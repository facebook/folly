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

#include <folly/synchronization/AtomicUtil.h>

#include <thread>
#include <utility>

#include <folly/Benchmark.h>
#include <folly/Portability.h>
#include <folly/Utility.h>
#include <folly/functional/Invoke.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/AtomicRef.h>

static constexpr auto relaxed = std::memory_order_relaxed;
static constexpr auto consume = std::memory_order_consume;
static constexpr auto acquire = std::memory_order_acquire;
static constexpr auto release = std::memory_order_release;
static constexpr auto acq_rel = std::memory_order_acq_rel;
static constexpr auto seq_cst = std::memory_order_seq_cst;

static_assert(
    std::is_same_v<int, folly::atomic_value_type_t<std::atomic<int>>>);
static_assert(
    std::is_same_v<int, folly::atomic_value_type<std::atomic<int>>::type>);

namespace folly {

class AtomicCompareExchangeSuccTest : public testing::Test {};

TEST_F(AtomicCompareExchangeSuccTest, examples) {
  using detail::atomic_compare_exchange_succ;

  // noop table
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(false, relaxed, relaxed));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(false, consume, relaxed));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(false, acquire, relaxed));
  EXPECT_EQ(release, atomic_compare_exchange_succ(false, release, relaxed));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(false, acq_rel, relaxed));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(false, seq_cst, relaxed));
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(false, relaxed, consume));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(false, consume, consume));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(false, acquire, consume));
  EXPECT_EQ(release, atomic_compare_exchange_succ(false, release, consume));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(false, acq_rel, consume));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(false, seq_cst, consume));
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(false, relaxed, acquire));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(false, consume, acquire));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(false, acquire, acquire));
  EXPECT_EQ(release, atomic_compare_exchange_succ(false, release, acquire));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(false, acq_rel, acquire));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(false, seq_cst, acquire));
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(false, relaxed, seq_cst));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(false, consume, seq_cst));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(false, acquire, seq_cst));
  EXPECT_EQ(release, atomic_compare_exchange_succ(false, release, seq_cst));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(false, acq_rel, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(false, seq_cst, seq_cst));

  // xform table
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(true, relaxed, relaxed));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(true, consume, relaxed));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, acquire, relaxed));
  EXPECT_EQ(release, atomic_compare_exchange_succ(true, release, relaxed));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, acq_rel, relaxed));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, seq_cst, relaxed));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(true, relaxed, consume));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(true, consume, consume));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, acquire, consume));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, release, consume));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, acq_rel, consume));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, seq_cst, consume));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, relaxed, acquire));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, consume, acquire));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, acquire, acquire));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, release, acquire));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, acq_rel, acquire));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, seq_cst, acquire));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, relaxed, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, consume, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, acquire, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, release, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, acq_rel, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, seq_cst, seq_cst));

  // properties
  for (auto succ : {relaxed, consume, acquire, release, acq_rel, seq_cst}) {
    SCOPED_TRACE(static_cast<int>(succ));
    for (auto fail : {relaxed, consume, acquire, seq_cst}) {
      SCOPED_TRACE(static_cast<int>(fail));
      EXPECT_EQ(succ, atomic_compare_exchange_succ(false, succ, fail));
      auto const sfix = atomic_compare_exchange_succ(true, succ, fail);
      EXPECT_GE(sfix, succ);
      EXPECT_GE(sfix, fail);
      EXPECT_TRUE(fail != relaxed || sfix == succ);
      EXPECT_TRUE(fail == relaxed || succ != release || sfix != release);
    }
  }
}

namespace {

template <typename L>
struct with_order : private L {
  std::memory_order o_;
  explicit with_order(std::memory_order o, L i) noexcept : L{i}, o_{o} {}
  template <typename... A>
  constexpr decltype(auto) operator()(A&&... a) const noexcept {
    return L::operator()(static_cast<A&&>(a)..., o_);
  }
};

template <typename>
struct atomic_ref_;

template <typename Integer>
struct atomic_ref_<std::atomic<Integer>> {
  using reference = std::atomic<Integer>&;
  std::atomic<Integer> value_;
  atomic_ref_() noexcept : value_{} {}
  constexpr explicit atomic_ref_(Integer value) noexcept : value_{value} {}
  constexpr operator reference() & { return reference(value_); }
};

template <typename Integer>
struct atomic_ref_<folly::atomic_ref<Integer>> {
  using reference = folly::atomic_ref<Integer>;
  Integer value_;
  atomic_ref_() noexcept : value_{} {}
  constexpr explicit atomic_ref_(Integer value) noexcept : value_{value} {}
  constexpr operator reference() & { return reference(value_); }
};

#if __cpp_lib_atomic_ref >= 201806L
template <typename Integer>
struct atomic_ref_<std::atomic_ref<Integer>> {
  using reference = std::atomic_ref<Integer>;
  Integer value_;
  atomic_ref_() noexcept : value_{} {}
  constexpr explicit atomic_ref_(Integer value) noexcept : value_{value} {}
  constexpr operator reference() & { return reference(value_); }
};
#endif

template <template <typename> class Atom>
struct atomic_ref_of {
  template <typename Integer>
  using apply = Atom<Integer>;
};

template <typename TypeParam, typename Integer, typename Op>
void atomic_fetch_set_basic(Op fetch_set) {
  using raw_ = typename TypeParam::template apply<Integer>;
  using obj_ = atomic_ref_<raw_>;
  using ref_ = typename obj_::reference;
  constexpr auto Size = 8 / (sizeof(Integer) % 16);
  static_assert(Size > 0);

  for (ref_ atomic : std::array<obj_, Size>{}) {
    atomic.store(0b0);
    EXPECT_EQ(fetch_set(atomic, 0), false);
    EXPECT_EQ(fetch_set(atomic, 1), false);
    EXPECT_EQ(atomic.load(), 0b11);
    EXPECT_EQ(fetch_set(atomic, 2), false);
    EXPECT_EQ(atomic.load(), 0b111);
  }

  for (ref_ atomic : std::array<obj_, Size>{}) {
    atomic.store(0b1);
    EXPECT_EQ(fetch_set(atomic, 0), true);
    EXPECT_EQ(fetch_set(atomic, 0), true);
    EXPECT_EQ(fetch_set(atomic, 1), false);
    EXPECT_EQ(atomic.load(), 0b11);
    EXPECT_EQ(fetch_set(atomic, 2), false);
    EXPECT_EQ(atomic.load(), 0b111);
  }

  {
    for (auto i = 0; i < 100000; ++i) {
      // call makeUnpredictable() to ensure that the bit integer does not get
      // optimized away.  This is testing the feasability of this code in
      // situations where bit is not known at compile time and will likely force
      // a register load
      obj_ atomic_{0b0};
      ref_ atomic = atomic_;
      auto&& bit = 0;
      folly::makeUnpredictable(bit);

      EXPECT_EQ(fetch_set(atomic, bit), false);
      EXPECT_EQ(fetch_set(atomic, bit + 1), false);
      EXPECT_EQ(atomic.load(), 0b11);
      EXPECT_EQ(fetch_set(atomic, bit + 2), false);
      EXPECT_EQ(atomic.load(), 0b111);
    }
  }
}

template <typename TypeParam, typename Integer, typename Op>
void atomic_fetch_reset_basic(Op fetch_reset) {
  using raw_ = typename TypeParam::template apply<Integer>;
  using obj_ = atomic_ref_<raw_>;
  using ref_ = typename obj_::reference;
  constexpr auto Size = 8 / (sizeof(Integer) % 16);
  static_assert(Size > 0);

  for (ref_ atomic : std::array<obj_, Size>{}) {
    EXPECT_EQ(fetch_reset(atomic, 0), false);
    EXPECT_EQ(fetch_reset(atomic, 1), false);
    atomic.store(0b11);
    EXPECT_EQ(fetch_reset(atomic, 0), true);
    EXPECT_EQ(fetch_reset(atomic, 1), true);
    EXPECT_EQ(atomic.load(), 0);
  }

  for (ref_ atomic : std::array<obj_, Size>{}) {
    EXPECT_EQ(fetch_reset(atomic, 0), false);
    EXPECT_EQ(fetch_reset(atomic, 1), false);
    atomic.store(0b11);
    EXPECT_EQ(fetch_reset(atomic, 1), true);
    EXPECT_EQ(fetch_reset(atomic, 0), true);
    EXPECT_EQ(atomic.load(), 0);
  }
}

template <typename TypeParam, typename Integer, typename Op>
void atomic_fetch_flip_basic(Op fetch_flip) {
  using raw_ = typename TypeParam::template apply<Integer>;
  using obj_ = atomic_ref_<raw_>;
  using ref_ = typename obj_::reference;
  constexpr auto Size = 8 / (sizeof(Integer) % 16);
  static_assert(Size > 0);

  for (ref_ atomic : std::array<obj_, Size>{}) {
    EXPECT_EQ(fetch_flip(atomic, 0), false);
    EXPECT_EQ(fetch_flip(atomic, 1), false);
    atomic.store(0b11);
    EXPECT_EQ(fetch_flip(atomic, 0), true);
    EXPECT_EQ(fetch_flip(atomic, 1), true);
    EXPECT_EQ(atomic.load(), 0);
  }

  for (ref_ atomic : std::array<obj_, Size>{}) {
    EXPECT_EQ(fetch_flip(atomic, 0), false);
    EXPECT_EQ(fetch_flip(atomic, 1), false);
    atomic.store(0b10);
    EXPECT_EQ(fetch_flip(atomic, 1), true);
    EXPECT_EQ(fetch_flip(atomic, 0), false);
    EXPECT_EQ(atomic.load(), 0b01);
  }
}

template <typename Integer>
class Atomic {
 public:
  using value_type = Integer;

  Integer fetch_or(Integer value, std::memory_order = seq_cst) {
    ++counts.set;
    return std::exchange(integer_, integer_ | value);
  }
  Integer fetch_and(Integer value, std::memory_order = seq_cst) {
    ++counts.reset;
    return std::exchange(integer_, integer_ & value);
  }
  Integer fetch_xor(Integer value, std::memory_order = seq_cst) {
    ++counts.flip;
    return std::exchange(integer_, integer_ ^ value);
  }

  Integer load(std::memory_order = seq_cst) { return integer_; }

  Integer integer_{0};

  struct counts_ {
    size_t set{0};
    size_t reset{0};
    size_t flip{0};
  };
  counts_ counts;
};

template <typename Integer, typename Op>
void atomic_fetch_set_non_std_atomic(Op fetch_set) {
  auto atomic = Atomic<Integer>{};
  auto& sets = atomic.counts.set;
  auto& resets = atomic.counts.reset;
  auto& flips = atomic.counts.flip;

  fetch_set(atomic, 0);
  EXPECT_EQ(sets, 1);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(flips, 0);
  EXPECT_EQ(atomic.integer_, 0b1);

  fetch_set(atomic, 2);
  EXPECT_EQ(sets, 2);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(flips, 0);
  EXPECT_EQ(atomic.integer_, 0b101);
}

template <typename Integer, typename Op>
void atomic_fetch_reset_non_std_atomic(Op fetch_reset) {
  auto atomic = Atomic<Integer>{};
  auto& sets = atomic.counts.set;
  auto& resets = atomic.counts.reset;
  auto& flips = atomic.counts.flip;
  atomic.integer_ = 0b111;

  fetch_reset(atomic, 0);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 1);
  EXPECT_EQ(flips, 0);
  EXPECT_EQ(atomic.integer_, 0b110);

  fetch_reset(atomic, 2);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 2);
  EXPECT_EQ(flips, 0);
  EXPECT_EQ(atomic.integer_, 0b010);
}

template <typename Integer, typename Op>
void atomic_fetch_flip_non_std_atomic(Op fetch_flip) {
  auto atomic = Atomic<Integer>{};
  auto& sets = atomic.counts.set;
  auto& resets = atomic.counts.reset;
  auto& flips = atomic.counts.flip;
  atomic.integer_ = 0b110;

  fetch_flip(atomic, 0);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(flips, 1);
  EXPECT_EQ(atomic.integer_, 0b111);

  fetch_flip(atomic, 2);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(flips, 2);
  EXPECT_EQ(atomic.integer_, 0b011);
}
} // namespace

template <typename Param>
class AtomicFetchSetTest : public ::testing::TestWithParam<Param> {};
template <typename Param>
class AtomicFetchResetTest : public ::testing::TestWithParam<Param> {};
template <typename Param>
class AtomicFetchFlipTest : public ::testing::TestWithParam<Param> {};

TYPED_TEST_SUITE_P(AtomicFetchSetTest);
TYPED_TEST_SUITE_P(AtomicFetchResetTest);
TYPED_TEST_SUITE_P(AtomicFetchFlipTest);

TYPED_TEST_P(AtomicFetchSetTest, Basic) {
  auto op = with_order{seq_cst, folly::atomic_fetch_set};

  atomic_fetch_set_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint8_t>(op);
}

TYPED_TEST_P(AtomicFetchResetTest, Basic) {
  auto op = with_order{seq_cst, folly::atomic_fetch_reset};

  atomic_fetch_reset_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint8_t>(op);
}

TYPED_TEST_P(AtomicFetchFlipTest, Basic) {
  auto op = with_order{seq_cst, folly::atomic_fetch_flip};

  atomic_fetch_flip_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint8_t>(op);
}

TYPED_TEST_P(AtomicFetchSetTest, BasicRelaxed) {
  auto op = with_order{relaxed, folly::atomic_fetch_set};

  atomic_fetch_set_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint8_t>(op);
}

TYPED_TEST_P(AtomicFetchResetTest, BasicRelaxed) {
  auto op = with_order{relaxed, folly::atomic_fetch_reset};

  atomic_fetch_reset_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint8_t>(op);
}

TYPED_TEST_P(AtomicFetchFlipTest, BasicRelaxed) {
  auto op = with_order{relaxed, folly::atomic_fetch_flip};

  atomic_fetch_flip_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint8_t>(op);
}

TYPED_TEST_P(AtomicFetchSetTest, EnsureFetchOrUsed) {
  auto op = with_order{seq_cst, folly::atomic_fetch_set};

  atomic_fetch_set_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint64_t>(op);
}

TYPED_TEST_P(AtomicFetchResetTest, EnsureFetchAndUsed) {
  auto op = with_order{seq_cst, folly::atomic_fetch_reset};

  atomic_fetch_reset_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint64_t>(op);
}

TYPED_TEST_P(AtomicFetchFlipTest, EnsureFetchXorUsed) {
  auto op = with_order{seq_cst, folly::atomic_fetch_flip};

  atomic_fetch_flip_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint64_t>(op);
}

TYPED_TEST_P(AtomicFetchSetTest, FetchSetFallback) {
  auto op = with_order{seq_cst, folly::detail::atomic_fetch_set_fallback};

  atomic_fetch_set_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint8_t>(op);

  atomic_fetch_set_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint64_t>(op);
}

TYPED_TEST_P(AtomicFetchResetTest, FetchResetFallback) {
  auto op = with_order{seq_cst, folly::detail::atomic_fetch_reset_fallback};

  atomic_fetch_reset_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint8_t>(op);

  atomic_fetch_reset_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint64_t>(op);
}

TYPED_TEST_P(AtomicFetchFlipTest, FetchFlipFallback) {
  auto op = with_order{seq_cst, folly::detail::atomic_fetch_flip_fallback};

  atomic_fetch_flip_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint8_t>(op);

  atomic_fetch_flip_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint64_t>(op);
}

TYPED_TEST_P(AtomicFetchSetTest, FetchSetDefault) {
  auto op = folly::atomic_fetch_set;

  atomic_fetch_set_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_set_basic<TypeParam, std::uint8_t>(op);

  atomic_fetch_set_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint64_t>(op);
}

TYPED_TEST_P(AtomicFetchResetTest, FetchResetDefault) {
  auto op = folly::atomic_fetch_reset;

  atomic_fetch_reset_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_reset_basic<TypeParam, std::uint8_t>(op);

  atomic_fetch_reset_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint64_t>(op);
}

TYPED_TEST_P(AtomicFetchFlipTest, FetchFlipDefault) {
  auto op = folly::atomic_fetch_flip;

  atomic_fetch_flip_basic<TypeParam, std::uint16_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint32_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint64_t>(op);
  atomic_fetch_flip_basic<TypeParam, std::uint8_t>(op);

  atomic_fetch_flip_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint64_t>(op);
}

REGISTER_TYPED_TEST_SUITE_P(
    AtomicFetchSetTest,
    Basic,
    BasicRelaxed,
    EnsureFetchOrUsed,
    FetchSetFallback,
    FetchSetDefault);

REGISTER_TYPED_TEST_SUITE_P(
    AtomicFetchResetTest,
    Basic,
    BasicRelaxed,
    EnsureFetchAndUsed,
    FetchResetFallback,
    FetchResetDefault);

REGISTER_TYPED_TEST_SUITE_P(
    AtomicFetchFlipTest,
    Basic,
    BasicRelaxed,
    EnsureFetchXorUsed,
    FetchFlipFallback,
    FetchFlipDefault);

INSTANTIATE_TYPED_TEST_SUITE_P(
    StdAtomic, AtomicFetchSetTest, atomic_ref_of<std::atomic>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    FollyAtomicRef, AtomicFetchSetTest, atomic_ref_of<folly::atomic_ref>);
#if __cpp_lib_atomic_ref >= 201806L
INSTANTIATE_TYPED_TEST_SUITE_P(
    StdAtomicRef, AtomicFetchSetTest, atomic_ref_of<std::atomic_ref>);
#endif

INSTANTIATE_TYPED_TEST_SUITE_P(
    StdAtomic, AtomicFetchResetTest, atomic_ref_of<std::atomic>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    FollyAtomicRef, AtomicFetchResetTest, atomic_ref_of<folly::atomic_ref>);
#if __cpp_lib_atomic_ref >= 201806L
INSTANTIATE_TYPED_TEST_SUITE_P(
    StdAtomicRef, AtomicFetchResetTest, atomic_ref_of<std::atomic_ref>);
#endif

INSTANTIATE_TYPED_TEST_SUITE_P(
    StdAtomic, AtomicFetchFlipTest, atomic_ref_of<std::atomic>);
INSTANTIATE_TYPED_TEST_SUITE_P(
    FollyAtomicRef, AtomicFetchFlipTest, atomic_ref_of<folly::atomic_ref>);
#if __cpp_lib_atomic_ref >= 201806L
INSTANTIATE_TYPED_TEST_SUITE_P(
    StdAtomicRef, AtomicFetchFlipTest, atomic_ref_of<std::atomic_ref>);
#endif

struct AtomicFetchModifyTest : testing::Test {};

TEST_F(AtomicFetchModifyTest, example) {
  constexpr auto prime255 = 1619;
  constexpr auto op = [=](auto _) { return (_ + 3) % prime255; };
  std::atomic<int> cell{2};
  auto const prev = folly::atomic_fetch_modify(cell, op, relaxed);
  EXPECT_EQ(2, prev);
  EXPECT_EQ(5, cell.load(relaxed));
}

TEST_F(AtomicFetchModifyTest, contention) {
  constexpr auto prime255 = 1619;
  constexpr size_t lg_nthreads = 6;
  constexpr size_t lg_required_contention = 9;
  constexpr auto iterate = [](auto v, size_t c, auto f) {
    while (c--) {
      v = f(v);
    }
    return v;
  };
  constexpr auto op_ = [=](auto _) { return (_ + 3) % prime255; };

  // run concurrent atomic-fetch-modify ops until enough contention is observed,
  // where contention observed is ~ number of times an op is repeated

  std::atomic<int> cell{2};
  std::atomic<size_t> iters{0};
  std::atomic<size_t> calls{0};
  auto const op = [&](auto const _) {
    calls.fetch_add(1, relaxed);
    return op_(_);
  };
  std::vector<std::thread> threads(1ULL << lg_nthreads);
  std::atomic<bool> stop{false};
  for (auto& th : threads) {
    th = std::thread([&] {
      while (!stop.load(relaxed)) {
        iters.fetch_add(1, relaxed); // incr first
        folly::atomic_fetch_modify(cell, op, relaxed);
      }
    });
  }

  constexpr auto required_contention =
      to_signed(1ULL << lg_required_contention);
  auto const contention = [&] {
    auto const c = folly::to_signed(calls.load(relaxed));
    auto const i = folly::to_signed(iters.load(relaxed));
    return c < prime255 || i < prime255 ? 0 : c - i;
  };
  while (contention() < required_contention)
    ;
  stop.store(true, relaxed);
  for (auto& th : threads) {
    th.join();
  }
  ASSERT_GE(contention(), required_contention);

  // compare the contended result to an expected uncontended result

  EXPECT_EQ(iterate(2, iters.load(relaxed), op_), cell.load(relaxed));
}

} // namespace folly
