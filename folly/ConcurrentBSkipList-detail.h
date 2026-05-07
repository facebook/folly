/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// ConcurrentBSkipList-detail.h — Internal seqlock primitive, node types, and
// allocator support for ConcurrentBSkipList. Included by ConcurrentBSkipList.h.
// Do NOT include directly.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <glog/logging.h>
#include <folly/Portability.h>
#include <folly/Utility.h>
#include <folly/lang/Align.h>
#include <folly/portability/Asm.h>
#include <folly/synchronization/RWSpinLock.h>
#include <folly/synchronization/RelaxedAtomic.h>
#include <folly/synchronization/SanitizeThread.h>

namespace folly {
// Separate (SoA): parallel keys_[] and payloads_[] arrays.
// Inline (AoS): interleaved {key, payload} records per slot.
enum class LeafStoragePolicy {
  Separate,
  Inline,
};

// How Skipper reads a key slot under concurrent writes. Default per T:
//   trivially-copyable + lock-free hardware atomic (≤8B) -> RelaxedAtomic
//   otherwise                                            -> Locked
enum class KeyReadPolicy {
  // Acquire the leaf shared mutex on every read. Always correct.
  Locked,
  // Atomic load (relaxed). T must be hardware-atomic + trivially copyable
  // (e.g. uint32_t, uint64_t, 16B struct on x86 with cmpxchg16b).
  RelaxedAtomic,
};

template <
    typename,
    typename,
    int,
    folly::KeyReadPolicy,
    folly::LeafStoragePolicy,
    typename>
class ConcurrentBSkipList;
} // namespace folly

namespace folly::bskip_detail {

// ---------------------------------------------------------------------------
// Seqlock for ConcurrentBSkipList OLC reader/writer bracketing.
//
// Standard seqlock protocol:
//   Writer: store(version+1, relaxed) → fence(release) → write data →
//           store(version+2, release)
//   Reader: load(version, acquire) → read data → fence(acquire) →
//           load(version, relaxed) → compare
//
// Key design choices:
//   - Typed field access (AtomicSlot<T>) instead of byte-at-a-time
//     memcpy. A 16-byte key read is one vmovdqa/ldp instruction.
//     Possible because we restrict keys to hardware-atomic types
//     (static_assert in InternalTraits).
//   - Spin-retry on odd version (writer in flight) before returning
//     to the caller, sized to absorb brief alloc-free insert epochs.
//   - RAII WriteGuard tied to the node's seqlock lifetime.
//
// Writers must be externally synchronized (node mutex held).
// ---------------------------------------------------------------------------

class Seqlock {
 public:
  class WriteGuard : folly::NonCopyableNonMovable {
   public:
    explicit WriteGuard(Seqlock& seq) : seq_(seq) { seq_.beginWrite(); }
    ~WriteGuard() { seq_.endWrite(); }

   private:
    Seqlock& seq_;
  };

  // NOTE: caller must hold the node mutex for the lifetime of this guard.
  // Only one writer may increment the seqlock at a time.
  [[nodiscard]] WriteGuard writeGuard() { return WriteGuard{*this}; }

  template <int kReadSpins>
  uint32_t readBegin() const {
    if constexpr (kReadSpins > 0) {
      for (int spin = 0; spin < kReadSpins; ++spin) {
        uint32_t v = version_.load(std::memory_order_acquire);
        if (!(v & 1)) {
          return v;
        }
        folly::asm_volatile_pause();
      }
    }
    return version_.load(std::memory_order_acquire);
  }

  bool validate(uint32_t v) const {
    std::atomic_thread_fence(std::memory_order_acquire);
    return version_.load(std::memory_order_relaxed) == v;
  }

  bool isInWriteEpoch() const noexcept {
    return (version_.load(std::memory_order_relaxed) & 1u) != 0;
  }

  // Writer is externally synchronized (node mutex held). Relaxed store is
  // sufficient; the release fence orders it before subsequent data writes.
  void beginWrite() {
    uint32_t prev = version_.load(std::memory_order_relaxed);
    DCHECK_EQ(prev & 1u, 0u) << "beginWrite while already in write epoch";
    DCHECK_LT(prev, std::numeric_limits<uint32_t>::max() - 1)
        << "version overflow";
    version_.store(prev + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
  }

  void endWrite() {
    version_.store(
        version_.load(std::memory_order_relaxed) + 1,
        std::memory_order_release);
  }

  void annotateVersionField(const char* file, int line) const {
    annotate_benign_race_sized(
        &version_,
        sizeof(version_),
        "BSkipList OLC: seqlock version field",
        file,
        line);
  }

 private:
  mutable std::atomic<uint32_t> version_;
};

// ---------------------------------------------------------------------------
// Node types, storage primitives, and allocator support
// ---------------------------------------------------------------------------

// Zero-size payload slot for [[no_unique_address]] in void-payload templates.
using Empty = std::monostate;

enum class BSkipTestHookEvent {
  ProbeNextLeafLoadNext,
  ProbeNextLeafValidateNext,
  OlcLoadNext,
  LeafKeyPostLoadValidate,
  SplitPublishComplete,
};

// Signature defined unconditionally so callers compile regardless of whether
// FOLLY_BSKIP_TEST_HOOKS is set; only bodies depend on the macro.
using BSkipTestHook = void (*)(
    BSkipTestHookEvent event,
    std::memory_order order,
    const void* node,
    const void* peer);

#ifdef FOLLY_BSKIP_TEST_HOOKS
inline std::atomic<BSkipTestHook> gBSkipTestHook{nullptr};

FOLLY_ALWAYS_INLINE void setBSkipTestHook(BSkipTestHook hook) {
  gBSkipTestHook.store(hook, std::memory_order_relaxed);
}

FOLLY_ALWAYS_INLINE void invokeBSkipTestHook(
    BSkipTestHookEvent event,
    std::memory_order order,
    const void* node,
    const void* peer) {
  if (auto hook = gBSkipTestHook.load(std::memory_order_relaxed)) {
    hook(event, order, node, peer);
  }
}
#else
FOLLY_ALWAYS_INLINE void setBSkipTestHook(BSkipTestHook) {}
FOLLY_ALWAYS_INLINE void invokeBSkipTestHook(
    BSkipTestHookEvent, std::memory_order, const void*, const void*) {}
#endif

// Tuning knobs separated from InternalTraits so they can be adjusted without
// touching the Traits template parameter set.
struct BSkipTuning {
  // Same-leaf optimistic-miss retries before broader escalation. Sized to
  // absorb a brief writer epoch.
  static constexpr int kSeqlockRetries = 3;
  // Cached-path OLC + one root-reload retry before falling back to locked HOH.
  static constexpr int kOlcAttempts = 2;
  // Spin budget for an active writer before AdaptiveReadGuard takes the
  // shared lock. ARM: fewer spins because isb is heavier than x86 pause.
  static constexpr int kReadSpins = folly::kIsArchAArch64 ? 48 : 256;
};

// ---------------------------------------------------------------------------
// Storage primitive for optimistic-read paths. Locked-mode uses plain T
// (leaf mutex provides exclusion); optimistic mode uses AtomicSlot<T>.
// ---------------------------------------------------------------------------

// Lock-free read slot for hardware-atomic T. The atomic load instruction
// IS the bracket: a writer's store either fully precedes or fully follows
// the reader's load and never tears. Readers need no surrounding protocol.
//
// Wraps folly::relaxed_atomic<T>; synthesized copy/move ops let
// std::array element-assignment compile in splitKeys/moveChildren (raw
// std::atomic isn't copyable).
//
// Alignment inherits from std::atomic<T> and is STRICTER than alignof(T)
// when the lock-free instruction is wider than T's natural alignment
// (e.g. 16-byte cmpxchg16b on an 8-byte-aligned 16-byte struct). Do NOT
// replace this wrapper with std::atomic_ref<T> over raw T storage:
// atomic_ref surrenders that alignment guarantee back to the caller AND
// regressed children_ traversal on 16B-key benchmarks (misaligned loads).
template <typename T>
struct AtomicSlot {
  AtomicSlot() = default;
  ~AtomicSlot() = default;
  /* implicit */ AtomicSlot(const T& v) : val_(v) {}
  AtomicSlot(const AtomicSlot& o) : val_(o.val_.load()) {}
  AtomicSlot(AtomicSlot&& o) noexcept : val_(o.val_.load()) {}
  AtomicSlot& operator=(const AtomicSlot& o) {
    val_.store(o.val_.load());
    return *this;
  }
  AtomicSlot& operator=(AtomicSlot&& o) noexcept {
    val_.store(o.val_.load());
    return *this;
  }
  AtomicSlot& operator=(const T& v) {
    val_.store(v);
    return *this;
  }
  /* implicit */ operator T() const { return val_.load(); }

  folly::relaxed_atomic<T> val_;
};

// Non-void PayloadT only (InternalTraits gates instantiation).
template <typename KeyStorage, typename PayloadT>
struct InlineLeafRecord {
  KeyStorage key{};
  PayloadT payload{};
};

namespace detail_hw_atomic {
// Concept so the && short-circuits: std::atomic<void>::is_always_lock_free
// is ill-formed, but trivially_copyable<void> fails first.
template <typename T>
concept Check =
    std::is_trivially_copyable_v<T> && std::atomic<T>::is_always_lock_free;
} // namespace detail_hw_atomic

template <typename T>
inline constexpr bool kIsHardwareAtomic = detail_hw_atomic::Check<T>;

// Optimistic readers may call Comp on stale-but-complete key values
// (hw-atomic guarantees no tearing). The comparator must be safe on any
// complete value — no pointer dereferences, no side effects.
template <typename T>
inline constexpr folly::KeyReadPolicy kDefaultReadPolicy = kIsHardwareAtomic<T>
    ? folly::KeyReadPolicy::RelaxedAtomic
    : folly::KeyReadPolicy::Locked;

// Picks the leaf-slot storage type for a key/value type T:
//   !kOptimistic  → plain T (read under leaf mutex)
//   kOptimistic   → AtomicSlot<T> (single-instruction relaxed atomic load)
template <typename T, bool kOptimistic>
using KeyStorage = std::conditional_t<!kOptimistic, T, AtomicSlot<T>>;

// Same as KeyStorage but Empty when there's no payload at all.
template <typename T, bool kHasValue, bool kOptimistic>
using PayloadStorage =
    std::conditional_t<!kHasValue, Empty, KeyStorage<T, kOptimistic>>;

template <
    typename T,
    typename Comp,
    int B,
    int P,
    folly::KeyReadPolicy ReadPolicy = kDefaultReadPolicy<T>,
    typename PayloadT = void,
    folly::LeafStoragePolicy StoragePolicy = folly::LeafStoragePolicy::Separate>
struct InternalTraits {
  static_assert(B > 1, "B (fanout) must be > 1");
  static_assert(P > 1, "P (promotion denominator) must be > 1");
  static_assert(
      std::numeric_limits<T>::is_specialized,
      "T must have std::numeric_limits specialization for sentinel values");
  static_assert(
      std::is_void_v<PayloadT> || std::is_trivially_copyable_v<PayloadT>,
      "PayloadType must be void or trivially copyable");
  static_assert(
      StoragePolicy != folly::LeafStoragePolicy::Inline ||
          !std::is_void_v<PayloadT>,
      "Inline storage requires a non-void PayloadType");
  static_assert(
      ReadPolicy != folly::KeyReadPolicy::RelaxedAtomic || kIsHardwareAtomic<T>,
      "RelaxedAtomic requires a hardware-atomic key type "
      "(e.g. uint32_t, uint64_t, 16B struct on x86)");
  static_assert(
      ReadPolicy == folly::KeyReadPolicy::Locked || std::is_void_v<PayloadT> ||
          kIsHardwareAtomic<PayloadT>,
      "Optimistic mode with payload requires a hardware-atomic payload type");
  using key_type = T;
  using payload_type = PayloadT;
  static constexpr bool kHasPayload = !std::is_void_v<PayloadT>;
  using PayloadPtr =
      std::conditional_t<kHasPayload, const PayloadT*, std::nullptr_t>;
  static constexpr uint8_t kMaxKeys = B;
  static constexpr uint8_t kPromotionProbInverse = P;
  static constexpr bool kOptimistic =
      ReadPolicy != folly::KeyReadPolicy::Locked;

  using KeyStorage = bskip_detail::KeyStorage<T, kOptimistic>;
  using PayloadStorage =
      bskip_detail::PayloadStorage<PayloadT, kHasPayload, kOptimistic>;
  static constexpr bool kInlinePayload =
      kHasPayload && StoragePolicy == folly::LeafStoragePolicy::Inline;
  static constexpr bool kHasSeparatePayload =
      kHasPayload && StoragePolicy == folly::LeafStoragePolicy::Separate;
  using TombstoneWord = std::conditional_t<
      (B <= 8),
      uint8_t,
      std::conditional_t<
          (B <= 16),
          uint16_t,
          std::conditional_t<(B <= 32), uint32_t, uint64_t>>>;
  static_assert(kMaxKeys <= sizeof(TombstoneWord) * 8);

  // Concrete "negative infinity" under Comp.
  static inline const key_type kMinSentinel =
      Comp{}(std::numeric_limits<T>::min(), std::numeric_limits<T>::max())
      ? std::numeric_limits<T>::min()
      : std::numeric_limits<T>::max();
  // Concrete "positive infinity" under Comp.
  static inline const key_type kMaxSentinel =
      Comp{}(std::numeric_limits<T>::min(), std::numeric_limits<T>::max())
      ? std::numeric_limits<T>::max()
      : std::numeric_limits<T>::min();

  template <typename L, typename R>
  FOLLY_ALWAYS_INLINE static bool less(const L& a, const R& b) {
    return Comp{}(a, b);
  }
  template <typename L, typename R>
  FOLLY_ALWAYS_INLINE static bool equal(const L& a, const R& b) {
    if constexpr (std::is_same_v<L, R> && requires(const L& x) { x == x; }) {
      return a == b;
    } else {
      return !Comp{}(a, b) && !Comp{}(b, a);
    }
  }
};

// Optimistic load: reads the slot, then validates. Returns nullopt if a
// writer epoch crossed the read (torn copy is discarded).
template <typename Value, typename Storage, typename Validator>
FOLLY_ALWAYS_INLINE std::optional<Value> loadValidated(
    const Storage& storage, Validator&& validate) {
  Value value = static_cast<Value>(storage);
  if (!validate()) {
    return std::nullopt;
  }
  return value;
}

template <typename NodeAlloc = std::allocator<char>>
struct BSkipAllocator {
  BSkipAllocator() = default;
  explicit BSkipAllocator(const NodeAlloc& a) : alloc(a) {}

  template <typename U>
  U* allocate() {
    using AllocType =
        typename std::allocator_traits<NodeAlloc>::template rebind_alloc<U>;
    AllocType typedAlloc(alloc);
    U* ptr = std::allocator_traits<AllocType>::allocate(typedAlloc, 1);
    std::allocator_traits<AllocType>::construct(typedAlloc, ptr);
    return ptr;
  }

  template <typename U>
  void deallocate(U* ptr) {
    using AllocType =
        typename std::allocator_traits<NodeAlloc>::template rebind_alloc<U>;
    AllocType typedAlloc(alloc);
    std::allocator_traits<AllocType>::destroy(typedAlloc, ptr);
    std::allocator_traits<AllocType>::deallocate(typedAlloc, ptr, 1);
  }

  [[no_unique_address]] NodeAlloc alloc;
};

template <typename Traits>
struct BSkipNode;
template <typename Traits>
class BSkipNodeLeaf;
template <typename Traits>
class BSkipNodeInternal;
template <typename Traits, int>
class AdaptiveReadGuard;

// Friend declaration for ConcurrentBSkipList; #undef at end of file.
#define FOLLY_BSKIP_FRIEND_LIST \
  template <                    \
      typename,                 \
      typename,                 \
      int,                      \
      folly::KeyReadPolicy,     \
      folly::LeafStoragePolicy, \
      typename>                 \
  friend class ::folly::ConcurrentBSkipList
template <typename Traits>
struct BSkipNode {
  using T = typename Traits::key_type;
  using KeyStorage = typename Traits::KeyStorage;
  using Seq = Seqlock;

  BSkipNode() { annotateBaseRaces(); }

  // Extracts T out of KeyStorage (AtomicSlot<T> or plain T).
  FOLLY_ALWAYS_INLINE T loadNextMinKey() const {
    return static_cast<T>(nextMinKey_);
  }

 protected:
  void annotateBaseRaces() {
    annotate_benign_race_sized(
        &next_,
        sizeof(next_),
        "BSkipList OLC: std::atomic next pointer; readers use acquire load",
        __FILE__,
        __LINE__);
    annotate_benign_race_sized(
        &nextMinKey_,
        sizeof(nextMinKey_),
        "BSkipList OLC: readers validate via version check",
        __FILE__,
        __LINE__);
    seq_.annotateVersionField(__FILE__, __LINE__);
    annotate_benign_race_sized(
        &numElements_,
        sizeof(numElements_),
        "BSkipList OLC: readers validate via version check",
        __FILE__,
        __LINE__);
  }

  FOLLY_BSKIP_FRIEND_LIST;
  template <typename, int>
  friend class AdaptiveReadGuard;

  // std::atomic: readers pair load(acquire) with the release in
  // publishSplitSibling.
  std::atomic<BSkipNode<Traits>*> next_;
  mutable Seq seq_;
  mutable folly::RWSpinLock mutex_;
  KeyStorage nextMinKey_{Traits::kMaxSentinel};
  folly::relaxed_atomic<uint8_t> numElements_;
  // Set once at allocation; immutable thereafter.
  uint8_t level_{0};
};

// Callers under HOH locks get correct keys. Callers on the optimistic
// (lockfree) path may read stale-but-complete keys (hw-atomic guaranteed);
// they must validate the seqlock after this returns.
template <typename Traits, typename LookupKey, typename LoadKey>
FOLLY_ALWAYS_INLINE uint8_t findFirstGreaterLinear(
    uint8_t begin, uint8_t end, const LookupKey& target, LoadKey&& loadKey) {
  uint8_t i = begin;
  for (; i < end; ++i) {
    if (Traits::less(target, loadKey(i))) {
      break;
    }
  }
  return i;
}

template <typename Traits, int kReadSpins = BSkipTuning::kReadSpins>
class AdaptiveReadGuard : folly::NonCopyableNonMovable {
 public:
  explicit AdaptiveReadGuard(BSkipNode<Traits>& node) : node_(node) {
    version_ = node_.seq_.template readBegin<kReadSpins>();
    if (!(version_ & 1)) {
      return;
    }
    node_.mutex_.lock_shared();
    version_ = node_.seq_.template readBegin<0>();
    holdingSharedLock_ = true;
  }

  ~AdaptiveReadGuard() { releaseSharedLockEarly(); }

  bool valid() const {
    if (holdingSharedLock_) {
      return true;
    }
    return node_.seq_.validate(version_);
  }

  // No-op on the pure-optimistic path. Releases the shared lock earlier than
  // the destructor would; clearing the flag prevents double-unlock.
  void releaseSharedLockEarly() {
    if (holdingSharedLock_) {
      node_.mutex_.unlock_shared();
      holdingSharedLock_ = false;
    }
  }

 private:
  BSkipNode<Traits>& node_;
  uint32_t version_ = 0;
  bool holdingSharedLock_ = false;
};

// Layout policy. Separate: parallel keys_/payloads_ arrays.
// Inline: AoS records_. BSkipNodeLeaf delegates here so the
// kInlinePayload branches live in one place.
template <typename Traits, bool kInline = Traits::kInlinePayload>
struct LeafStorage;

template <typename Traits>
struct LeafStorage<Traits, /*kInline=*/false> {
  using T = typename Traits::key_type;
  using KeyStorage = typename Traits::KeyStorage;
  using PayloadStorage = typename Traits::PayloadStorage;
  using KeyArray = std::array<KeyStorage, Traits::kMaxKeys>;
  using PayloadArray = std::conditional_t<
      Traits::kHasSeparatePayload,
      std::array<PayloadStorage, Traits::kMaxKeys>,
      Empty>;

  KeyArray keys_{};
  [[no_unique_address]] PayloadArray payloads_{};

  FOLLY_ALWAYS_INLINE T loadKey(uint8_t slot) const {
    return static_cast<T>(keys_[slot]);
  }
  FOLLY_ALWAYS_INLINE void storeKey(uint8_t slot, const T& key) {
    keys_[slot] = key;
  }

  template <typename PayloadT>
  FOLLY_ALWAYS_INLINE PayloadT loadPayload(uint8_t slot) const {
    return static_cast<PayloadT>(payloads_[slot]);
  }
  template <typename PayloadT>
  FOLLY_ALWAYS_INLINE void storePayload(uint8_t slot, const PayloadT& payload) {
    payloads_[slot] = payload;
  }

  FOLLY_ALWAYS_INLINE const KeyStorage& keyStorageAt(uint8_t slot) const {
    return keys_[slot];
  }
  FOLLY_ALWAYS_INLINE KeyStorage& keyStorageAt(uint8_t slot) {
    return keys_[slot];
  }
  template <bool HasPayload = Traits::kHasSeparatePayload>
  FOLLY_ALWAYS_INLINE const PayloadStorage& payloadStorageAt(uint8_t slot) const
    requires(HasPayload)
  {
    return payloads_[slot];
  }
  template <bool HasPayload = Traits::kHasSeparatePayload>
  FOLLY_ALWAYS_INLINE PayloadStorage& payloadStorageAt(uint8_t slot)
    requires(HasPayload)
  {
    return payloads_[slot];
  }

  void shiftElementsRight(uint8_t slot, uint8_t numElements) {
    for (uint8_t i = numElements; i > slot; --i) {
      keys_[i] = keys_[i - 1];
    }
    if constexpr (Traits::kHasSeparatePayload) {
      for (uint8_t i = numElements; i > slot; --i) {
        payloads_[i] = payloads_[i - 1];
      }
    }
  }

  void clearElement(uint8_t slot) {
    keys_[slot] = KeyStorage{};
    if constexpr (Traits::kHasSeparatePayload) {
      payloads_[slot] = PayloadStorage{};
    }
  }

  void annotateRaces(const char* file, int line) const {
    annotate_benign_race_sized(
        &keys_,
        sizeof(keys_),
        "BSkipList seqlock: readers validate via version check",
        file,
        line);
    if constexpr (Traits::kHasSeparatePayload) {
      annotate_benign_race_sized(
          &payloads_,
          sizeof(payloads_),
          "BSkipList seqlock: payload reads validate via version check",
          file,
          line);
    }
  }
};

template <typename Traits>
struct LeafStorage<Traits, /*kInline=*/true> {
  using T = typename Traits::key_type;

  std::array<
      InlineLeafRecord<
          typename Traits::KeyStorage,
          typename Traits::PayloadStorage>,
      Traits::kMaxKeys>
      records_{};

  FOLLY_ALWAYS_INLINE T loadKey(uint8_t slot) const {
    return static_cast<T>(records_[slot].key);
  }
  FOLLY_ALWAYS_INLINE void storeKey(uint8_t slot, const T& key) {
    records_[slot].key = key;
  }

  template <typename PayloadT>
  FOLLY_ALWAYS_INLINE PayloadT loadPayload(uint8_t slot) const {
    return static_cast<PayloadT>(records_[slot].payload);
  }
  template <typename PayloadT>
  FOLLY_ALWAYS_INLINE void storePayload(uint8_t slot, const PayloadT& payload) {
    records_[slot].payload = payload;
  }

  FOLLY_ALWAYS_INLINE const typename Traits::KeyStorage& keyStorageAt(
      uint8_t slot) const {
    return records_[slot].key;
  }
  template <typename PayloadT = typename Traits::payload_type>
  FOLLY_ALWAYS_INLINE const typename Traits::PayloadStorage& payloadStorageAt(
      uint8_t slot) const
    requires(!std::is_void_v<PayloadT>)
  {
    return records_[slot].payload;
  }

  void shiftElementsRight(uint8_t slot, uint8_t numElements) {
    for (uint8_t i = numElements; i > slot; --i) {
      records_[i] = records_[i - 1];
    }
  }

  void clearElement(uint8_t slot) { records_[slot] = {}; }

  void annotateRaces(const char* file, int line) const {
    annotate_benign_race_sized(
        &records_,
        sizeof(records_),
        "BSkipList seqlock: readers validate inline key/payload storage",
        file,
        line);
  }
};

template <typename Traits>
class BSkipNodeLeaf : public BSkipNode<Traits> {
  using typename BSkipNode<Traits>::T;

 public:
  struct LeafSearchResult {
    // exactMatch=true: slot of the match. Otherwise: predecessor slot for
    // the insert (n-1 if the new key would go past the end).
    uint8_t slot = 0;
    bool exactMatch = false;
  };

  BSkipNodeLeaf() {
    annotate_benign_race_sized(
        &this->tombstones_,
        sizeof(this->tombstones_),
        "BSkipList seqlock: readers validate via version check",
        __FILE__,
        __LINE__);
    storage_.annotateRaces(__FILE__, __LINE__);
  }

  [[nodiscard]] T minKey() const { return loadKey(0); }

  FOLLY_ALWAYS_INLINE T loadKey(uint8_t slot) const {
    return storage_.loadKey(slot);
  }
  FOLLY_ALWAYS_INLINE const typename Traits::KeyStorage& keyStorageAt(
      uint8_t slot) const {
    return storage_.keyStorageAt(slot);
  }

  FOLLY_ALWAYS_INLINE void storeKey(uint8_t slot, const T& key) {
    storage_.storeKey(slot, key);
  }

  template <typename RequestedPayload = typename Traits::payload_type>
  FOLLY_ALWAYS_INLINE RequestedPayload loadPayload(uint8_t slot) const
    requires(Traits::kHasPayload && !std::is_void_v<RequestedPayload>)
  {
    return storage_.template loadPayload<RequestedPayload>(slot);
  }
  template <typename RequestedPayload = typename Traits::payload_type>
  FOLLY_ALWAYS_INLINE const typename Traits::PayloadStorage& payloadStorageAt(
      uint8_t slot) const
    requires(Traits::kHasPayload && !std::is_void_v<RequestedPayload>)
  {
    return storage_.payloadStorageAt(slot);
  }

  template <typename RequestedPayload = typename Traits::payload_type>
  FOLLY_ALWAYS_INLINE void storePayload(
      uint8_t slot, const RequestedPayload& payload)
    requires(Traits::kHasPayload && !std::is_void_v<RequestedPayload>)
  {
    storage_.template storePayload<RequestedPayload>(slot, payload);
  }

  template <typename LookupKey>
  LeafSearchResult findLeafSlot(const LookupKey& k) const {
    const uint8_t n = this->numElements_.load();
    DCHECK_GT(n, 0u) << "sentinel guarantees numElements_ >= 1";
    // HOH invariant: the crab-lock traversal guarantees k >= keys[0] before
    // entering this node. If this DCHECK fires, the descent chose the wrong
    // node — a bug in the traversal, not in this function.
    DCHECK(!Traits::less(k, loadKey(0)))
        << "findLeafSlot called with k < minKey — HOH "
           "invariant violated";
    // Differs from findChild's findFirstGreaterLinear call: we
    // need the loaded key value (not just its slot) for the equality check,
    // so inlining avoids a second loadKey per match.
    for (uint8_t i = 0; i < n; ++i) {
      T key = loadKey(i);
      if (!Traits::less(key, k)) {
        if (Traits::equal(key, k)) {
          return {.slot = i, .exactMatch = true};
        }
        return {.slot = (i > 0) ? static_cast<uint8_t>(i - 1) : uint8_t{0}};
      }
    }
    return {.slot = static_cast<uint8_t>(n - 1)};
  }

  uint8_t splitKeys(
      BSkipNodeLeaf<Traits>* dest, uint8_t splitIndex, uint8_t destSlot) {
    DCHECK(this->seq_.isInWriteEpoch());
    DCHECK(dest->seq_.isInWriteEpoch());
    // destSlot=0: overflow split, dest is empty.
    // destSlot=1: promoted split, dest already has slot 0 populated.
    DCHECK_EQ(dest->numElements_.load(), destSlot);
    DCHECK_EQ(dest->tombstones_.load(), 0u);
    uint8_t oldNumElements = this->numElements_.load();
    uint8_t numElementsToMove = oldNumElements - splitIndex;
    DCHECK_LE(destSlot + numElementsToMove, Traits::kMaxKeys);

    for (uint8_t i = 0; i < numElementsToMove; i++) {
      copyElementTo(dest, splitIndex + i, destSlot + i);
    }

    // Shift guards below (e.g. splitIndex < kTWBits) prevent UB on
    // over-wide shifts; they are always true since kMaxKeys <= kTWBits
    // by static_assert above.
    using TW = typename Traits::TombstoneWord;
    constexpr int kTWBits = sizeof(TW) * 8;
    TW srcTombstones = tombstones_.load();
    TW movedTombstoneBits =
        splitIndex < kTWBits ? srcTombstones >> splitIndex : 0;
    if (numElementsToMove < kTWBits) {
      movedTombstoneBits &= (TW{1} << numElementsToMove) - 1;
    }
    TW destTombstoneBits =
        destSlot < kTWBits ? movedTombstoneBits << destSlot : 0;
    dest->tombstones_.store(destTombstoneBits);
    TW srcKeepMask = splitIndex < kTWBits ? (TW{1} << splitIndex) - 1 : ~TW{0};
    tombstones_.store(srcTombstones & srcKeepMask);

    this->numElements_.store(splitIndex);
    // Both nodes exclusively owned; plain store is sufficient.
    dest->numElements_.store(dest->numElements_.load() + numElementsToMove);

    for (uint8_t i = splitIndex; i < oldNumElements; ++i) {
      storage_.clearElement(i);
    }

    return numElementsToMove;
  }

  // Run under leaf mutex + seqlock write epoch; relaxed load.
  bool tombstoned(uint8_t slot) const {
    return (tombstones_.load() >> slot) & 1;
  }
  // Run under leaf mutex + seqlock write epoch; relaxed store.
  void setTombstone(uint8_t slot) {
    DCHECK(this->seq_.isInWriteEpoch());
    using TW = typename Traits::TombstoneWord;
    TW current = tombstones_.load();
    DCHECK_EQ((current >> slot) & 1, 0u)
        << "setTombstone on already-tombstoned";
    tombstones_.store(current | (TW{1} << slot));
  }
  void clearTombstone(uint8_t slot) {
    DCHECK(this->seq_.isInWriteEpoch());
    using TW = typename Traits::TombstoneWord;
    TW current = tombstones_.load();
    DCHECK_EQ((current >> slot) & 1, 1u)
        << "clearTombstone on already-cleared slot";
    tombstones_.store(current & ~(TW{1} << slot));
  }

  AdaptiveReadGuard<Traits> adaptiveRead() {
    return AdaptiveReadGuard<Traits>{*this};
  }

  using PayloadPtr = typename Traits::PayloadPtr;

  // Insert key + payload + bump numElements. Caller must already be in a
  // seqlock write epoch (either via writeGuard or beginSplitSiblingWrite).
  void insertKeyAtSlotRaw(
      uint8_t slot, const T& key, PayloadPtr payload = nullptr) {
    DCHECK(this->seq_.isInWriteEpoch());
    insertKeyAtSlot(slot, key);
    setPayload(slot, payload);
    this->numElements_.fetch_add(1);
  }

  void insertKeyAtSlotGuarded(
      uint8_t slot, const T& key, PayloadPtr payload = nullptr) {
    auto guard = this->seq_.writeGuard();
    insertKeyAtSlotRaw(slot, key, payload);
  }

 private:
  FOLLY_BSKIP_FRIEND_LIST;

  void insertKeyAtSlot(uint8_t slot, const T& key) {
    using TW = typename Traits::TombstoneWord;
    // Caller must hold the seqlock write epoch (the keys_/payloads_ TSAN
    // benign-race annotation would otherwise hide a write-without-epoch bug).
    DCHECK(this->seq_.isInWriteEpoch());
    DCHECK_LT(slot, sizeof(TW) * 8) << "slot would shift past TW";
    const uint8_t numElements = this->numElements_.load();
    DCHECK_LE(numElements + 1, Traits::kMaxKeys);
    // Bit-shift below assumes tombstones at slots >= numElements are zero.
    DCHECK(
        numElements >= sizeof(TW) * 8 ||
        (tombstones_.load() >> numElements) == TW{0});
    storage_.shiftElementsRight(slot, numElements);
    TW upperMask = ~((TW{1} << slot) - 1u);
    TW tb = tombstones_.load();
    TW lower = tb & ~upperMask;
    TW upper = tb & upperMask;
    tombstones_.store(lower | (upper << 1));
    storeKey(slot, key);
  }

  void copyElementTo(
      BSkipNodeLeaf<Traits>* dest, uint8_t srcSlot, uint8_t destSlot) const {
    dest->storeKey(destSlot, loadKey(srcSlot));
    if constexpr (Traits::kHasPayload) {
      dest->storePayload(destSlot, loadPayload(srcSlot));
    }
  }

  // Always writes (default-constructed if null) so insertKeyAtSlot doesn't
  // need to pre-clear — one store per epoch instead of two.
  void setPayload(uint8_t slot, PayloadPtr payload) {
    if constexpr (Traits::kHasPayload) {
      storePayload(slot, payload ? *payload : typename Traits::payload_type{});
    }
  }

  folly::relaxed_atomic<typename Traits::TombstoneWord> tombstones_;
  LeafStorage<Traits> storage_{};
};

template <typename Traits>
class BSkipNodeInternal : public BSkipNode<Traits> {
  using typename BSkipNode<Traits>::T;
  using typename BSkipNode<Traits>::KeyStorage;

 public:
  using KeyArray = std::array<KeyStorage, Traits::kMaxKeys>;

  struct InternalSearchResult {
    // Child index to descend into. For inserts, the new key goes at slot + 1.
    uint8_t slot = 0;
    // Item at keys_[slot] equals the search key.
    bool found = false;
  };

  BSkipNodeInternal() {
    annotate_benign_race_sized(
        &keys_,
        sizeof(keys_),
        "BSkipList seqlock: readers validate via version check",
        __FILE__,
        __LINE__);
    annotate_benign_race_sized(
        &this->children_,
        sizeof(this->children_),
        "BSkipList OLC: readers validate via version check",
        __FILE__,
        __LINE__);
  }

  [[nodiscard]] T minKey() const { return static_cast<T>(keys_[0]); }

  // Returns {child index to descend into, whether k was found as a routing
  // key}.
  template <typename LookupKey>
  InternalSearchResult findChild(const LookupKey& k) const {
    const uint8_t n = this->numElements_.load();
    // OLC readers may observe a preallocated node before it's populated.
    if (n == 0) {
      return {};
    }
    uint8_t i = bskip_detail::findFirstGreaterLinear<Traits>(
        1, n, k, [&](uint8_t slot) { return static_cast<T>(keys_[slot]); });
    InternalSearchResult result;
    result.slot = static_cast<uint8_t>(i - 1);
    result.found = Traits::equal(static_cast<T>(keys_[result.slot]), k);
    return result;
  }

  void splitKeysAndChildren(
      BSkipNodeInternal<Traits>* dest, uint8_t splitIndex, uint8_t destSlot) {
    DCHECK(this->seq_.isInWriteEpoch());
    DCHECK(dest->seq_.isInWriteEpoch());
    DCHECK_EQ(dest->numElements_.load(), destSlot);
    uint8_t numElementsToMove = this->numElements_.load() - splitIndex;
    DCHECK_LE(destSlot + numElementsToMove, Traits::kMaxKeys);

    for (uint8_t i = 0; i < numElementsToMove; ++i) {
      dest->keys_[destSlot + i] = keys_[splitIndex + i];
      dest->children_[destSlot + i] = children_[splitIndex + i];
    }

    this->numElements_.store(splitIndex);
    dest->numElements_.store(dest->numElements_.load() + numElementsToMove);
  }

  void insertKeyAtSlot(uint8_t slot, const T& key) {
    DCHECK(this->seq_.isInWriteEpoch());
    DCHECK_LE(this->numElements_.load() + 1, Traits::kMaxKeys);

    for (uint8_t i = this->numElements_.load(); i > slot; --i) {
      keys_[i] = keys_[i - 1];
    }
    keys_[slot] = key;
  }

  // Caller increments numElements_ after this returns; postInsertCount is the
  // shift range (current count + 1).
  void insertChildAtSlot(uint8_t insertionSlot, BSkipNode<Traits>* child) {
    DCHECK(this->seq_.isInWriteEpoch());
    const uint8_t postInsertCount = this->numElements_.load() + 1;
    DCHECK_LE(postInsertCount, Traits::kMaxKeys);
    for (uint8_t i = postInsertCount - 1; i > insertionSlot; --i) {
      children_[i] = children_[i - 1];
    }
    children_[insertionSlot] = child;
  }

 private:
  FOLLY_BSKIP_FRIEND_LIST;
  // B-tree internal node layout: keys_[i] is the separator between
  // children_[i-1] and children_[i]. keys_[0] is this node's minimum
  // routing key, set at creation. Search starts at keys_[1] because
  // target >= keys_[0] is guaranteed by the HOH descent from the parent.
  KeyArray keys_{};
  std::array<AtomicSlot<BSkipNode<Traits>*>, Traits::kMaxKeys> children_{};
};

#undef FOLLY_BSKIP_FRIEND_LIST

} // namespace folly::bskip_detail
