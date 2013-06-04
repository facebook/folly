/*
 * Copyright 2013 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// @author Mark Rabkin (mrabkin@fb.com)
// @author Andrei Alexandrescu (andrei.alexandrescu@fb.com)

#include "folly/Range.h"

#include <emmintrin.h>  // __v16qi
#include <iostream>

namespace folly {

/**
 * Predicates that can be used with qfind and startsWith
 */
const AsciiCaseSensitive asciiCaseSensitive = AsciiCaseSensitive();
const AsciiCaseInsensitive asciiCaseInsensitive = AsciiCaseInsensitive();

std::ostream& operator<<(std::ostream& os, const StringPiece& piece) {
  os.write(piece.start(), piece.size());
  return os;
}

namespace detail {

size_t qfind_first_byte_of_memchr(const StringPiece& haystack,
                                  const StringPiece& needles) {
  size_t best = haystack.size();
  for (char needle: needles) {
    const void* ptr = memchr(haystack.data(), needle, best);
    if (ptr) {
      auto found = static_cast<const char*>(ptr) - haystack.data();
      best = std::min<size_t>(best, found);
    }
  }
  if (best == haystack.size()) {
    return StringPiece::npos;
  }
  return best;
}

}  // namespace detail

namespace {

// It's okay if pages are bigger than this (as powers of two), but they should
// not be smaller.
constexpr size_t kMinPageSize = 4096;
static_assert(kMinPageSize >= 16,
              "kMinPageSize must be at least SSE register size");
#define PAGE_FOR(addr) \
  (reinterpret_cast<uintptr_t>(addr) / kMinPageSize)


#if FOLLY_HAVE_EMMINTRIN_H
inline size_t nextAlignedIndex(const char* arr) {
   auto firstPossible = reinterpret_cast<uintptr_t>(arr) + 1;
   return 1 +                       // add 1 because the index starts at 'arr'
     ((firstPossible + 15) & ~0xF)  // round up to next multiple of 16
     - firstPossible;
}

// build sse4.2-optimized version even if -msse4.2 is not passed to GCC
size_t qfind_first_byte_of_needles16(const StringPiece& haystack,
                                     const StringPiece& needles)
  __attribute__ ((__target__("sse4.2"), noinline));

// helper method for case where needles.size() <= 16
size_t qfind_first_byte_of_needles16(const StringPiece& haystack,
                                     const StringPiece& needles) {
  DCHECK(!haystack.empty());
  DCHECK(!needles.empty());
  DCHECK_LE(needles.size(), 16);
  // benchmarking shows that memchr beats out SSE for small needle-sets
  // with large haystacks.
  if ((needles.size() <= 2 && haystack.size() >= 256) ||
      // must bail if we can't even SSE-load a single segment of haystack
      (haystack.size() < 16 &&
       PAGE_FOR(haystack.end() - 1) != PAGE_FOR(haystack.data() + 15)) ||
      // can't load needles into SSE register if it could cross page boundary
      PAGE_FOR(needles.end() - 1) != PAGE_FOR(needles.data() + 15)) {
    return detail::qfind_first_byte_of_memchr(haystack, needles);
  }

  auto arr2 = __builtin_ia32_loaddqu(needles.data());
  // do an unaligned load for first block of haystack
  auto arr1 = __builtin_ia32_loaddqu(haystack.data());
  auto index = __builtin_ia32_pcmpestri128(arr2, needles.size(),
                                           arr1, haystack.size(), 0);
  if (index < 16) {
    return index;
  }

  // Now, we can do aligned loads hereafter...
  size_t i = nextAlignedIndex(haystack.data());
  for (; i < haystack.size(); i+= 16) {
    void* ptr1 = __builtin_assume_aligned(haystack.data() + i, 16);
    auto arr1 = *reinterpret_cast<const __v16qi*>(ptr1);
    auto index = __builtin_ia32_pcmpestri128(arr2, needles.size(),
                                             arr1, haystack.size() - i, 0);
    if (index < 16) {
      return i + index;
    }
  }
  return StringPiece::npos;
}
#endif // FOLLY_HAVE_EMMINTRIN_H

// Aho, Hopcroft, and Ullman refer to this trick in "The Design and Analysis
// of Computer Algorithms" (1974), but the best description is here:
// http://research.swtch.com/sparse
class FastByteSet {
 public:
  FastByteSet() : size_(0) { }  // no init of arrays required!

  inline void add(uint8_t i) {
    if (!contains(i)) {
      dense_[size_] = i;
      sparse_[i] = size_;
      size_++;
    }
  }
  inline bool contains(uint8_t i) const {
    DCHECK_LE(size_, 256);
    return sparse_[i] < size_ && dense_[sparse_[i]] == i;
  }

 private:
  uint16_t size_;  // can't use uint8_t because it would overflow if all
                   // possible values were inserted.
  uint8_t sparse_[256];
  uint8_t dense_[256];
};

}  // namespace

namespace detail {

size_t qfind_first_byte_of_byteset(const StringPiece& haystack,
                                   const StringPiece& needles) {
  FastByteSet s;
  for (auto needle: needles) {
    s.add(needle);
  }
  for (size_t index = 0; index < haystack.size(); ++index) {
    if (s.contains(haystack[index])) {
      return index;
    }
  }
  return StringPiece::npos;
}

#if FOLLY_HAVE_EMMINTRIN_H

template <bool HAYSTACK_ALIGNED>
inline size_t scanHaystackBlock(const StringPiece& haystack,
                                const StringPiece& needles,
                                int64_t idx)
// inline is okay because it's only called from other sse4.2 functions
  __attribute__ ((__target__("sse4.2")));

// Scans a 16-byte block of haystack (starting at blockStartIdx) to find first
// needle. If HAYSTACK_ALIGNED, then haystack must be 16byte aligned.
// If !HAYSTACK_ALIGNED, then caller must ensure that it is safe to load the
// block.
template <bool HAYSTACK_ALIGNED>
inline size_t scanHaystackBlock(const StringPiece& haystack,
                                const StringPiece& needles,
                                int64_t blockStartIdx) {
  DCHECK_GT(needles.size(), 16);  // should handled by *needles16() method
  DCHECK(blockStartIdx + 16 <= haystack.size() ||
         (PAGE_FOR(haystack.data() + blockStartIdx) ==
          PAGE_FOR(haystack.data() + blockStartIdx + 15)));

  __v16qi arr1;
  if (HAYSTACK_ALIGNED) {
    void* ptr1 = __builtin_assume_aligned(haystack.data() + blockStartIdx, 16);
    arr1 = *reinterpret_cast<const __v16qi*>(ptr1);
  } else {
    arr1 = __builtin_ia32_loaddqu(haystack.data() + blockStartIdx);
  }

  // This load is safe because needles.size() >= 16
  auto arr2 = __builtin_ia32_loaddqu(needles.data());
  size_t b = __builtin_ia32_pcmpestri128(
    arr2, 16, arr1, haystack.size() - blockStartIdx, 0);

  size_t j = nextAlignedIndex(needles.data());
  for (; j < needles.size(); j += 16) {
    void* ptr2 = __builtin_assume_aligned(needles.data() + j, 16);
    arr2 = *reinterpret_cast<const __v16qi*>(ptr2);

    auto index = __builtin_ia32_pcmpestri128(
      arr2, needles.size() - j, arr1, haystack.size() - blockStartIdx, 0);
    b = std::min<size_t>(index, b);
  }

  if (b < 16) {
    return blockStartIdx + b;
  }
  return StringPiece::npos;
}

size_t qfind_first_byte_of_sse42(const StringPiece& haystack,
                                 const StringPiece& needles)
  __attribute__ ((__target__("sse4.2"), noinline));

size_t qfind_first_byte_of_sse42(const StringPiece& haystack,
                                 const StringPiece& needles) {
  if (UNLIKELY(needles.empty() || haystack.empty())) {
    return StringPiece::npos;
  } else if (needles.size() <= 16) {
    // we can save some unnecessary load instructions by optimizing for
    // the common case of needles.size() <= 16
    return qfind_first_byte_of_needles16(haystack, needles);
  }

  if (haystack.size() < 16 &&
      PAGE_FOR(haystack.end() - 1) != PAGE_FOR(haystack.data() + 16)) {
    // We can't safely SSE-load haystack. Use a different approach.
    if (haystack.size() <= 2) {
      return qfind_first_of(haystack, needles, asciiCaseSensitive);
    }
    return qfind_first_byte_of_byteset(haystack, needles);
  }

  auto ret = scanHaystackBlock<false>(haystack, needles, 0);
  if (ret != StringPiece::npos) {
    return ret;
  }

  size_t i = nextAlignedIndex(haystack.data());
  for (; i < haystack.size(); i += 16) {
    auto ret = scanHaystackBlock<true>(haystack, needles, i);
    if (ret != StringPiece::npos) {
      return ret;
    }
  }

  return StringPiece::npos;
}
#endif // FOLLY_HAVE_EMMINTRIN_H

size_t qfind_first_byte_of_nosse(const StringPiece& haystack,
                                 const StringPiece& needles) {
  if (UNLIKELY(needles.empty() || haystack.empty())) {
    return StringPiece::npos;
  }
  // The thresholds below were empirically determined by benchmarking.
  // This is not an exact science since it depends on the CPU, the size of
  // needles, and the size of haystack.
  if (haystack.size() == 1 ||
      (haystack.size() < 4 && needles.size() <= 16)) {
    return qfind_first_of(haystack, needles, asciiCaseSensitive);
  } else if ((needles.size() >= 4 && haystack.size() <= 10) ||
             (needles.size() >= 16 && haystack.size() <= 64) ||
             needles.size() >= 32) {
    return qfind_first_byte_of_byteset(haystack, needles);
  }

  return qfind_first_byte_of_memchr(haystack, needles);
}

}  // namespace detail
}  // namespace folly
