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
#include "folly/Likely.h"

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
#define PAGE_FOR(addr) \
  (reinterpret_cast<intptr_t>(addr) / kMinPageSize)

// Rounds up to the next multiple of 16
#define ROUND_UP_16(val) \
  ((val + 15) & ~0xF)

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
  if ((needles.size() <= 2 && haystack.size() >= 256) ||
      // we can't load needles into SSE register if it could cross page boundary
      (PAGE_FOR(needles.end() - 1) != PAGE_FOR(needles.data() + 15))) {
    // benchmarking shows that memchr beats out SSE for small needle-sets
    // with large haystacks.
    // TODO(mcurtiss): could this be because of unaligned SSE loads?
    return detail::qfind_first_byte_of_memchr(haystack, needles);
  }

  __v16qi arr2 = __builtin_ia32_loaddqu(needles.data());

  // If true, the last byte we want to load into the SSE register is on the
  // same page as the last byte of the actual Range.  No risk of segfault.
  bool canSseLoadLastBlock =
    (PAGE_FOR(haystack.end() - 1) ==
     PAGE_FOR(haystack.data() + ROUND_UP_16(haystack.size()) - 1));
  int64_t lastSafeBlockIdx = canSseLoadLastBlock ?
    haystack.size() : static_cast<int64_t>(haystack.size()) - 16;

  int64_t i = 0;
  for (; i < lastSafeBlockIdx; i+= 16) {
    auto arr1 = __builtin_ia32_loaddqu(haystack.data() + i);
    auto index = __builtin_ia32_pcmpestri128(arr2, needles.size(),
                                             arr1, haystack.size() - i, 0);
    if (index < 16) {
      return i + index;
    }
  }

  if (!canSseLoadLastBlock) {
    StringPiece tmp(haystack);
    tmp.advance(i);
    auto ret = detail::qfind_first_byte_of_memchr(tmp, needles);
    if (ret != StringPiece::npos) {
      return ret + i;
    }
  }
  return StringPiece::npos;
}

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

inline size_t scanHaystackBlock(const StringPiece& haystack,
                                const StringPiece& needles,
                                int64_t idx)
// inlining is okay because it's only called from other sse4.2 functions
  __attribute__ ((__target__("sse4.2")));

// Scans a 16-byte block of haystack (starting at blockStartIdx) to find first
// needle. If blockStartIdx is near the end of haystack, it may read a few bytes
// past the end; it is the caller's responsibility to ensure this is safe.
inline size_t scanHaystackBlock(const StringPiece& haystack,
                                const StringPiece& needles,
                                int64_t blockStartIdx) {
  // small needle sets should be handled by qfind_first_byte_of_needles16()
  DCHECK_GT(needles.size(), 16);
  DCHECK(blockStartIdx + 16 <= haystack.size() ||
         (PAGE_FOR(haystack.data() + blockStartIdx) ==
          PAGE_FOR(haystack.data() + blockStartIdx + 15)));
  size_t b = 16;
  auto arr1 = __builtin_ia32_loaddqu(haystack.data() + blockStartIdx);
  int64_t j = 0;
  for (; j < static_cast<int64_t>(needles.size()) - 16; j += 16) {
    auto arr2 = __builtin_ia32_loaddqu(needles.data() + j);
    auto index = __builtin_ia32_pcmpestri128(
      arr2, 16, arr1, haystack.size() - blockStartIdx, 0);
    b = std::min<size_t>(index, b);
  }

  // Avoid reading any bytes past the end needles by just reading the last
  // 16 bytes of needles. We know this is safe because needles.size() > 16.
  auto arr2 = __builtin_ia32_loaddqu(needles.end() - 16);
  auto index = __builtin_ia32_pcmpestri128(
    arr2, 16, arr1, haystack.size() - blockStartIdx, 0);
  b = std::min<size_t>(index, b);

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

  int64_t i = 0;
  for (; i < static_cast<int64_t>(haystack.size()) - 16; i += 16) {
    auto ret = scanHaystackBlock(haystack, needles, i);
    if (ret != StringPiece::npos) {
      return ret;
    }
  };

  if (i == haystack.size() - 16 ||
      PAGE_FOR(haystack.end() - 1) == PAGE_FOR(haystack.data() + i + 15)) {
    return scanHaystackBlock(haystack, needles, i);
  } else {
    auto ret = qfind_first_byte_of_nosse(StringPiece(haystack.data() + i,
                                                     haystack.end()),
                                         needles);
    if (ret != StringPiece::npos) {
      return i + ret;
    }
  }

  return StringPiece::npos;
}

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
