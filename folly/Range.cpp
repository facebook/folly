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

// build sse4.2-optimized version even if -msse4.2 is not passed to GCC
size_t qfind_first_byte_of_needles16(const StringPiece& haystack,
                                     const StringPiece& needles)
  __attribute__ ((__target__("sse4.2"), noinline));

// helper method for case where needles.size() <= 16
size_t qfind_first_byte_of_needles16(const StringPiece& haystack,
                                     const StringPiece& needles) {
  DCHECK_LE(needles.size(), 16);
  if (needles.size() <= 2 && haystack.size() >= 256) {
    // benchmarking shows that memchr beats out SSE for small needle-sets
    // with large haystacks.
    // TODO(mcurtiss): could this be because of unaligned SSE loads?
    return detail::qfind_first_byte_of_memchr(haystack, needles);
  }
  auto arr2 = __builtin_ia32_loaddqu(needles.data());
  for (size_t i = 0; i < haystack.size(); i+= 16) {
    auto arr1 = __builtin_ia32_loaddqu(haystack.data() + i);
    auto index = __builtin_ia32_pcmpestri128(arr2, needles.size(),
                                             arr1, haystack.size() - i, 0);
    if (index < 16) {
      return i + index;
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

  size_t index = haystack.size();
  for (size_t i = 0; i < haystack.size(); i += 16) {
    size_t b = 16;
    auto arr1 = __builtin_ia32_loaddqu(haystack.data() + i);
    for (size_t j = 0; j < needles.size(); j += 16) {
      auto arr2 = __builtin_ia32_loaddqu(needles.data() + j);
      auto index = __builtin_ia32_pcmpestri128(arr2, needles.size() - j,
                                               arr1, haystack.size() - i, 0);
      b = std::min<size_t>(index, b);
    }
    if (b < 16) {
      return i + b;
    }
  };
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
