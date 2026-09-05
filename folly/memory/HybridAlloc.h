#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/lang/SafeAssert.h>
#include <folly/memory/Malloc.h>

#if defined(_MSC_VER)
#include <malloc.h>
#define FOLLY_ALLOCA(size) _alloca(size)
#elif defined(__GNUC__) || defined(__clang__)
#define FOLLY_ALLOCA(size) __builtin_alloca(size)
#else
#error "FOLLY_ALLOCA is not available for this compiler"
#endif

#ifndef FOLLY_HYBRID_ALLOC_DEFAULT_THRESHOLD
#define FOLLY_HYBRID_ALLOC_DEFAULT_THRESHOLD 1024
#endif

#ifndef FOLLY_HYBRID_ALLOC_MAX_STACK_ALIGNMENT
#define FOLLY_HYBRID_ALLOC_MAX_STACK_ALIGNMENT 4096
#endif

#define FOLLY_HYBRID_ALLOC_DOUBLE_FREE_CHECK 1

namespace folly {

/// Header stored immediately before each hybrid allocation.
///
/// The user pointer is placed after this header, rounded up to the requested
/// alignment. `raw_ptr` is the address that must be passed back to the
/// allocator when the storage came from the heap.
struct alignas(max_align_v) HybridAllocHeader {
  uint32_t marker; // origin and state of this allocation
  void* raw_ptr; // base pointer used for free (may differ from user ptr)
  size_t alloc_size; // total allocated size
  size_t alignment; // alignment used for this allocation
};

inline constexpr size_t kHybridAllocHeaderSize = sizeof(HybridAllocHeader);

static_assert(
    kHybridAllocHeaderSize % max_align_v == 0,
    "HybridAllocHeader size must be multiple of max_align_t");

/// Marker values distinguish stack vs heap origin and detect double-frees.
inline constexpr uint32_t kHybridAllocStackMarker = 0xCCCC;
inline constexpr uint32_t kHybridAllocHeapMarker = 0xDDDD;
inline constexpr uint32_t kHybridAlignedAllocStackMarker = 0xEEEE;
inline constexpr uint32_t kHybridAlignedAllocHeapMarker = 0xFFFF;
inline constexpr uint32_t kHybridAllocFreedHeapMarker = 0xDEAD;
inline constexpr uint32_t kHybridAllocFreedStackMarker = 0xBEEF;

namespace detail {

inline constexpr bool isHybridAllocStackMarker(uint32_t marker) noexcept {
  return marker == kHybridAllocStackMarker ||
      marker == kHybridAlignedAllocStackMarker;
}

inline constexpr bool isHybridAllocHeapMarker(uint32_t marker) noexcept {
  return marker == kHybridAllocHeapMarker ||
      marker == kHybridAlignedAllocHeapMarker;
}

inline constexpr bool isHybridAllocFreedMarker(uint32_t marker) noexcept {
  return marker == kHybridAllocFreedHeapMarker ||
      marker == kHybridAllocFreedStackMarker;
}

// Raises sub-max_align_t alignments up to max_align_v so that the header
// placement logic stays uniform.
inline constexpr size_t normalizeHybridAllocAlignment(
    size_t alignment) noexcept {
  return alignment < max_align_v ? max_align_v : alignment;
}

inline constexpr bool validHybridAllocAlignment(size_t alignment) noexcept {
  return valid_align_value(alignment);
}

/// Computes the total bytes needed for a hybrid allocation.
///
/// Includes the user payload, header, and enough padding to satisfy alignment.
/// Returns false for invalid alignment or overflow.
inline constexpr bool hybridAllocTotalSize(
    size_t size, size_t alignment, size_t* total) noexcept {
  if (!validHybridAllocAlignment(alignment)) {
    return false;
  }

  alignment = normalizeHybridAllocAlignment(alignment);
  constexpr auto kMax = std::numeric_limits<size_t>::max();
  const size_t padding = alignment - 1;
  if (size > kMax - kHybridAllocHeaderSize - padding) {
    return false;
  }

  *total = size + kHybridAllocHeaderSize + padding;
  return true;
}

// Like hybridAllocTotalSize but returns 0 on overflow.
inline constexpr size_t hybridAllocTotalSizeOrZero(
    size_t size, size_t alignment) noexcept {
  size_t total = 0;
  hybridAllocTotalSize(size, alignment, &total);
  return total;
}

// Whether the request fits the stack threshold after accounting for overhead.
inline bool hybridAllocUseStack(
    size_t size, size_t threshold, size_t alignment) noexcept {
  size_t total;
  return size <= threshold && hybridAllocTotalSize(size, alignment, &total);
}

// Debug assertion that the requested alignment is within the stack limit.
inline void checkHybridStackAlignment(size_t alignment) noexcept {
  FOLLY_SAFE_DCHECK(
      normalizeHybridAllocAlignment(alignment) <=
          FOLLY_HYBRID_ALLOC_MAX_STACK_ALIGNMENT,
      "hybrid allocation stack alignment is too large",
      uint64_t{alignment});
}

/// Initializes a raw allocation and returns the aligned user pointer.
///
/// Writes the header immediately before the returned pointer. `freePtr` is
/// null for stack storage and the heap base pointer otherwise.
inline void* initHybridAlloc(
    void* raw,
    size_t alignment,
    uint32_t marker,
    void* freePtr,
    size_t allocSize) noexcept {
  if (raw == nullptr) {
    return nullptr;
  }

  const size_t normalizedAlignment = normalizeHybridAllocAlignment(alignment);
  assert(validHybridAllocAlignment(alignment));

  auto* user = align_ceil(
      static_cast<unsigned char*>(raw) + kHybridAllocHeaderSize,
      normalizedAlignment);
  auto* header =
      reinterpret_cast<HybridAllocHeader*>(user - kHybridAllocHeaderSize);
  header->marker = marker;
  header->raw_ptr = freePtr;
  header->alloc_size = allocSize;
  header->alignment = normalizedAlignment;
  return user;
}

// Wrapper for the common stack-allocation path; raw_ptr is set to nullptr.
inline void* initHybridStackAlloc(
    void* raw, size_t alignment, uint32_t marker, size_t allocSize) noexcept {
  return initHybridAlloc(raw, alignment, marker, nullptr, allocSize);
}

// Allocates total bytes via malloc and initialises the header.
inline void* hybridHeapAllocWithTotal(
    size_t total, size_t alignment, uint32_t marker) noexcept {
  void* raw = std::malloc(total);
  return initHybridAlloc(raw, alignment, marker, raw, total);
}

// Computes total size, then allocates on the heap.
inline void* hybridHeapAlloc(
    size_t size, size_t alignment, uint32_t marker) noexcept {
  size_t total;
  if (!hybridAllocTotalSize(size, alignment, &total)) {
    return nullptr;
  }

  return hybridHeapAllocWithTotal(total, alignment, marker);
}

// Retrieve the header preceding a user pointer.
inline HybridAllocHeader* hybridAllocHeader(void* ptr) noexcept {
  return reinterpret_cast<HybridAllocHeader*>(
      static_cast<unsigned char*>(ptr) - kHybridAllocHeaderSize);
}

inline const HybridAllocHeader* hybridAllocHeader(const void* ptr) noexcept {
  return reinterpret_cast<const HybridAllocHeader*>(
      static_cast<const unsigned char*>(ptr) - kHybridAllocHeaderSize);
}

} // namespace detail

/// Frees memory allocated by the FOLLY_HYBRID_ALLOC* macros.
///
/// Stack allocations are a no-op; heap allocations are released with
/// sizedFree(). Double-free checking is enabled in debug builds.
inline void hybridFree(void* ptr) noexcept {
  if (ptr == nullptr) {
    return;
  }

  auto* header = detail::hybridAllocHeader(ptr);
  const auto marker = header->marker;
#if FOLLY_HYBRID_ALLOC_DOUBLE_FREE_CHECK
  FOLLY_SAFE_CHECK(
      !detail::isHybridAllocFreedMarker(marker),
      "hybrid allocation was already freed",
      uint64_t{marker});
#endif

  if (detail::isHybridAllocHeapMarker(marker)) {
#if FOLLY_HYBRID_ALLOC_DOUBLE_FREE_CHECK
    header->marker = kHybridAllocFreedHeapMarker;
#endif
    sizedFree(header->raw_ptr, header->alloc_size);
    return;
  }

  FOLLY_SAFE_CHECK(
      detail::isHybridAllocStackMarker(marker),
      "invalid hybrid allocation marker",
      uint64_t{marker});
#if FOLLY_HYBRID_ALLOC_DOUBLE_FREE_CHECK
  header->marker = kHybridAllocFreedStackMarker;
#endif
}

/// Returns the recorded allocation size, including header and padding.
inline size_t hybridGetTotalSize(const void* ptr) noexcept {
  if (ptr == nullptr) {
    return 0;
  }
  return detail::hybridAllocHeader(ptr)->alloc_size;
}

/// Returns a heap-backed copy of a hybrid allocation.
///
/// Heap allocations are returned unchanged. Stack allocations are copied to a
/// new heap buffer; the original stack storage is left in place.
inline void* hybridMarkPersistent(void* ptr, size_t size) noexcept {
  if (ptr == nullptr) {
    return nullptr;
  }

  auto* header = detail::hybridAllocHeader(ptr);
  const auto marker = header->marker;
#if FOLLY_HYBRID_ALLOC_DOUBLE_FREE_CHECK
  FOLLY_SAFE_CHECK(
      !detail::isHybridAllocFreedMarker(marker),
      "hybrid allocation was already freed",
      uint64_t{marker});
#endif

  if (detail::isHybridAllocHeapMarker(marker)) {
    return ptr;
  }

  FOLLY_SAFE_CHECK(
      detail::isHybridAllocStackMarker(marker),
      "invalid hybrid allocation marker",
      uint64_t{marker});

  const auto heapMarker = marker == kHybridAlignedAllocStackMarker
      ? kHybridAlignedAllocHeapMarker
      : kHybridAllocHeapMarker;
  void* persistent =
      detail::hybridHeapAlloc(size, header->alignment, heapMarker);
  if (persistent == nullptr) {
    return nullptr;
  }
  std::memcpy(persistent, ptr, size);
  return persistent;
}

} // namespace folly

/// @def FOLLY_HYBRID_ALLOC_THRESHOLD(size, threshold)
/// Allocates bytes on the stack when `size <= threshold`, otherwise on heap.
///
/// The returned pointer is compatible with hybridFree(). On MSVC, avoid
/// passing expressions with side effects.
#if defined(__GNUC__) || defined(__clang__)
#define FOLLY_HYBRID_ALLOC_THRESHOLD(size, threshold)                         \
  __extension__({                                                             \
    size_t const __folly_hybrid_size = (size);                                \
    size_t const __folly_hybrid_threshold = (threshold);                      \
    size_t const __folly_hybrid_alignment = ::folly::max_align_v;             \
    size_t __folly_hybrid_total = 0;                                          \
    void* __folly_hybrid_result = nullptr;                                    \
    if (::folly::detail::hybridAllocTotalSize(                                \
            __folly_hybrid_size,                                              \
            __folly_hybrid_alignment,                                         \
            &__folly_hybrid_total)) {                                         \
      if (__folly_hybrid_size <= __folly_hybrid_threshold) {                  \
        ::folly::detail::checkHybridStackAlignment(__folly_hybrid_alignment); \
        __folly_hybrid_result = ::folly::detail::initHybridStackAlloc(        \
            FOLLY_ALLOCA(__folly_hybrid_total),                               \
            __folly_hybrid_alignment,                                         \
            ::folly::kHybridAllocStackMarker,                                 \
            __folly_hybrid_total);                                            \
      } else {                                                                \
        __folly_hybrid_result = ::folly::detail::hybridHeapAllocWithTotal(    \
            __folly_hybrid_total,                                             \
            __folly_hybrid_alignment,                                         \
            ::folly::kHybridAllocHeapMarker);                                 \
      }                                                                       \
    }                                                                         \
    __folly_hybrid_result;                                                    \
  })
#elif defined(_MSC_VER)
// MSVC has no statement-expression equivalent. Keep FOLLY_ALLOCA at the call
// site, and avoid passing expressions with side effects to these macros.
#define FOLLY_HYBRID_ALLOC_THRESHOLD(size, threshold)                       \
  (::folly::detail::hybridAllocUseStack(                                    \
       (size), (threshold), ::folly::max_align_v)                           \
       ? (::folly::detail::checkHybridStackAlignment(::folly::max_align_v), \
          ::folly::detail::initHybridStackAlloc(                            \
              FOLLY_ALLOCA(                                                 \
                  ::folly::detail::hybridAllocTotalSizeOrZero(              \
                      (size), ::folly::max_align_v)),                       \
              ::folly::max_align_v,                                         \
              ::folly::kHybridAllocStackMarker,                             \
              ::folly::detail::hybridAllocTotalSizeOrZero(                  \
                  (size), ::folly::max_align_v)))                           \
       : ::folly::detail::hybridHeapAlloc(                                  \
             (size), ::folly::max_align_v, ::folly::kHybridAllocHeapMarker))
#endif

/// Convenience wrapper using the default hybrid allocation threshold.
#define FOLLY_HYBRID_ALLOC(size) \
  FOLLY_HYBRID_ALLOC_THRESHOLD(size, FOLLY_HYBRID_ALLOC_DEFAULT_THRESHOLD)

/// @def FOLLY_HYBRID_ALIGNED_ALLOC_THRESHOLD(size, alignment, threshold)
/// Like FOLLY_HYBRID_ALLOC_THRESHOLD, with an explicit alignment.
///
/// Stack alignment is capped at FOLLY_HYBRID_ALLOC_MAX_STACK_ALIGNMENT.
#if defined(__GNUC__) || defined(__clang__)
#define FOLLY_HYBRID_ALIGNED_ALLOC_THRESHOLD(size, alignment, threshold)      \
  __extension__({                                                             \
    size_t const __folly_hybrid_size = (size);                                \
    size_t const __folly_hybrid_threshold = (threshold);                      \
    size_t const __folly_hybrid_alignment = (alignment);                      \
    size_t __folly_hybrid_total = 0;                                          \
    void* __folly_hybrid_result = nullptr;                                    \
    if (::folly::detail::hybridAllocTotalSize(                                \
            __folly_hybrid_size,                                              \
            __folly_hybrid_alignment,                                         \
            &__folly_hybrid_total)) {                                         \
      if (__folly_hybrid_size <= __folly_hybrid_threshold) {                  \
        ::folly::detail::checkHybridStackAlignment(__folly_hybrid_alignment); \
        __folly_hybrid_result = ::folly::detail::initHybridStackAlloc(        \
            FOLLY_ALLOCA(__folly_hybrid_total),                               \
            __folly_hybrid_alignment,                                         \
            ::folly::kHybridAlignedAllocStackMarker,                          \
            __folly_hybrid_total);                                            \
      } else {                                                                \
        __folly_hybrid_result = ::folly::detail::hybridHeapAllocWithTotal(    \
            __folly_hybrid_total,                                             \
            __folly_hybrid_alignment,                                         \
            ::folly::kHybridAlignedAllocHeapMarker);                          \
      }                                                                       \
    }                                                                         \
    __folly_hybrid_result;                                                    \
  })
#elif defined(_MSC_VER)
// MSVC has no statement-expression equivalent. Keep FOLLY_ALLOCA at the call
// site, and avoid passing expressions with side effects to these macros.
#define FOLLY_HYBRID_ALIGNED_ALLOC_THRESHOLD(size, alignment, threshold)       \
  (::folly::detail::hybridAllocUseStack((size), (threshold), (alignment))      \
       ? ::folly::detail::initHybridStackAlloc(                                \
             (::folly::detail::checkHybridStackAlignment((alignment)),         \
              FOLLY_ALLOCA(                                                    \
                  ::folly::detail::hybridAllocTotalSizeOrZero(                 \
                      (size), (alignment)))),                                  \
             (alignment),                                                      \
             ::folly::kHybridAlignedAllocStackMarker,                          \
             ::folly::detail::hybridAllocTotalSizeOrZero((size), (alignment))) \
       : ::folly::detail::hybridHeapAlloc(                                     \
             (size), (alignment), ::folly::kHybridAlignedAllocHeapMarker))
#endif

/// Convenience wrapper for aligned allocation with the default threshold.
#define FOLLY_HYBRID_ALIGNED_ALLOC(size, alignment) \
  FOLLY_HYBRID_ALIGNED_ALLOC_THRESHOLD(             \
      size, alignment, FOLLY_HYBRID_ALLOC_DEFAULT_THRESHOLD)
