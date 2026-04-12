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

#pragma once

#include <cstdint>
#include <string_view>

namespace folly {
namespace regex {
namespace detail {

constexpr bool isDigit(char c) noexcept {
  return c >= '0' && c <= '9';
}

constexpr bool isAlpha(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool isWordChar(char c) noexcept {
  return isAlpha(c) || isDigit(c) || c == '_';
}

constexpr bool isSpace(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
      c == '\v';
}

// A single contiguous character range [lo, hi] inclusive.
struct CharRange {
  unsigned char lo = 0;
  unsigned char hi = 0;

  constexpr bool operator==(const CharRange& o) const noexcept {
    return lo == o.lo && hi == o.hi;
  }
};

// Test character membership against a flat array of sorted non-overlapping
// ranges.
constexpr bool charClassTest(
    const CharRange* ranges, int count, char c) noexcept {
  auto uc = static_cast<unsigned char>(c);
  for (int i = 0; i < count; ++i) {
    if (uc < ranges[i].lo) {
      return false;
    }
    if (uc <= ranges[i].hi) {
      return true;
    }
  }
  return false;
}

// Set bits in a bitmap for a single range [range_lo, range_hi], where
// the bitmap is offset by bitmap_lo. Uses O(words_touched) operations
// instead of O(chars) by computing whole-word masks.
constexpr void setRangeBits(
    uint64_t* words,
    unsigned char bitmap_lo,
    unsigned char range_lo,
    unsigned char range_hi) noexcept {
  int lo = range_lo - bitmap_lo;
  int hi = range_hi - bitmap_lo;
  int startWord = lo / 64;
  int endWord = hi / 64;
  int startBit = lo % 64;
  int endBit = hi % 64;

  if (startWord == endWord) {
    // Single word: create mask from startBit to endBit
    int nbits = endBit - startBit + 1;
    uint64_t mask =
        (nbits >= 64) ? ~uint64_t(0) : ((uint64_t(1) << nbits) - 1) << startBit;
    words[startWord] |= mask;
  } else {
    // Start word: set bits from startBit to 63
    words[startWord] |= ~uint64_t(0) << startBit;
    // Middle words: set all bits
    for (int w = startWord + 1; w < endWord; ++w) {
      words[w] = ~uint64_t(0);
    }
    // End word: set bits from 0 to endBit
    uint64_t endMask =
        (endBit >= 63) ? ~uint64_t(0) : (uint64_t(1) << (endBit + 1)) - 1;
    words[endWord] |= endMask;
  }
}

// Test character membership against a compact bitmap that covers only
// [lo, lo + word_count*64). O(1) runtime — single word lookup.
constexpr bool compactBitmapTest(
    const uint64_t* words, unsigned char lo, int word_count, char c) noexcept {
  auto uc = static_cast<unsigned char>(c);
  if (uc < lo) {
    return false;
  }
  int shifted = uc - lo;
  if (shifted >= word_count * 64) {
    return false;
  }
  return (words[shifted / 64] >> (shifted % 64)) & 1;
}

// Linked block for CharRangeSet's chain. Each block holds up to 4 ranges.
struct CharRangeBlock {
  static constexpr int kCapacity = 4;
  CharRange ranges[kCapacity] = {};
  int count = 0;
  CharRangeBlock* next = nullptr;
};

// Transient character class builder using a linked chain of small blocks.
// Used during constexpr parsing and optimization, then flattened into
// ParseResult's compact pool. The chain structure is hidden from callers
// via forEach() iteration.
struct CharRangeSet {
  CharRangeBlock first_ = {};
  CharRangeBlock* tail_ = &first_;
  int total_count_ = 0;

  CharRangeSet(const CharRangeSet&) = delete;
  CharRangeSet& operator=(const CharRangeSet&) = delete;

  constexpr CharRangeSet(CharRangeSet&& other) noexcept
      : first_(other.first_), total_count_(other.total_count_) {
    if (other.first_.next) {
      first_.next = other.first_.next;
      other.first_.next = nullptr;
    }
    tail_ = (first_.next) ? findTail() : &first_;
    other.tail_ = &other.first_;
    other.total_count_ = 0;
  }

  CharRangeSet& operator=(CharRangeSet&&) = delete;

  constexpr CharRangeSet() = default;

  constexpr ~CharRangeSet() {
    CharRangeBlock* b = first_.next;
    while (b) {
      CharRangeBlock* next = b->next;
      delete b;
      b = next;
    }
  }

  constexpr CharRangeSet clone() const {
    CharRangeSet out;
    forEach([&](CharRange r) { out.appendRaw(r); });
    return out;
  }

  constexpr int count() const noexcept { return total_count_; }

  // Materialize the complement ranges: walk the sorted ranges and emit gaps.
  constexpr void invert() noexcept {
    CharRange sorted[256];
    int n = 0;
    {
      const CharRangeBlock* b = &first_;
      while (b) {
        for (int i = 0; i < b->count; ++i) {
          if (n < 256)
            sorted[n++] = b->ranges[i];
        }
        b = b->next;
      }
    }

    clear();

    unsigned char prev = 0;
    for (int i = 0; i < n; ++i) {
      if (sorted[i].lo > prev) {
        appendRaw({prev, static_cast<unsigned char>(sorted[i].lo - 1)});
      }
      if (sorted[i].hi == 255) {
        return;
      }
      prev = static_cast<unsigned char>(sorted[i].hi + 1);
    }
    appendRaw({prev, 255});
  }

  constexpr bool test(char c) const noexcept {
    auto uc = static_cast<unsigned char>(c);
    const CharRangeBlock* b = &first_;
    while (b) {
      for (int i = 0; i < b->count; ++i) {
        if (uc < b->ranges[i].lo) {
          return false;
        }
        if (uc <= b->ranges[i].hi) {
          return true;
        }
      }
      b = b->next;
    }
    return false;
  }

  constexpr int popcount() const noexcept {
    int total = 0;
    forEach([&](CharRange r) {
      total += static_cast<int>(r.hi) - static_cast<int>(r.lo) + 1;
    });
    return total;
  }

  constexpr bool operator==(const CharRangeSet& o) const noexcept {
    if (total_count_ != o.total_count_) {
      return false;
    }
    const CharRangeBlock* a = &first_;
    const CharRangeBlock* b = &o.first_;
    int ai = 0, bi = 0;
    int remaining = total_count_;
    while (remaining > 0) {
      if (!(a->ranges[ai] == b->ranges[bi])) {
        return false;
      }
      --remaining;
      ++ai;
      ++bi;
      if (ai >= a->count) {
        a = a->next;
        ai = 0;
      }
      if (bi >= b->count) {
        b = b->next;
        bi = 0;
      }
    }
    return true;
  }

  template <typename Fn>
  constexpr void forEach(Fn&& fn) const {
    const CharRangeBlock* b = &first_;
    while (b) {
      for (int i = 0; i < b->count; ++i) {
        fn(b->ranges[i]);
      }
      b = b->next;
    }
  }

  constexpr void addChar(unsigned char c) { addRange(c, c); }

  constexpr void addRange(unsigned char lo, unsigned char hi) {
    int n = total_count_;
    CharRange* buf = nullptr;
    bool onStack = (n + 1 <= 32);
    CharRange stackBuf[32];
    if (onStack) {
      buf = stackBuf;
    } else {
      buf = new CharRange[n + 1];
    }

    int pos = 0;
    bool inserted = false;
    forEach([&](CharRange r) {
      if (!inserted && lo <= r.lo) {
        buf[pos++] = {lo, hi};
        inserted = true;
      }
      buf[pos++] = r;
    });
    if (!inserted) {
      buf[pos++] = {lo, hi};
    }

    int out = 0;
    for (int i = 0; i < pos; ++i) {
      if (out > 0 && buf[i].lo <= buf[out - 1].hi + 1) {
        if (buf[i].hi > buf[out - 1].hi) {
          buf[out - 1].hi = buf[i].hi;
        }
      } else {
        buf[out++] = buf[i];
      }
    }

    clear();
    for (int i = 0; i < out; ++i) {
      appendRaw(buf[i]);
    }

    if (!onStack) {
      delete[] buf;
    }
  }

  constexpr void merge(const CharRangeSet& other) {
    other.forEach([&](CharRange r) { addRange(r.lo, r.hi); });
  }

 private:
  constexpr void appendRaw(CharRange r) {
    if (tail_->count >= CharRangeBlock::kCapacity) {
      auto* nb = new CharRangeBlock();
      tail_->next = nb;
      tail_ = nb;
    }
    tail_->ranges[tail_->count++] = r;
    ++total_count_;
  }

  constexpr void clear() {
    CharRangeBlock* b = first_.next;
    while (b) {
      CharRangeBlock* next = b->next;
      delete b;
      b = next;
    }
    first_.count = 0;
    first_.next = nullptr;
    tail_ = &first_;
    total_count_ = 0;
  }

  constexpr CharRangeBlock* findTail() noexcept {
    CharRangeBlock* b = &first_;
    while (b->next) {
      b = b->next;
    }
    return b;
  }
};

// Lightweight filter for quickly rejecting starting positions that
// can't match the pattern's first character. Stores a small sorted
// array of ranges; overflows to accepts_all=true (conservative).
struct FirstCharFilter {
  static constexpr int kMaxRanges = 8;
  CharRange ranges[kMaxRanges] = {};
  int range_count = 0;
  bool accepts_all = true;

  constexpr bool test(char c) const noexcept {
    if (accepts_all) {
      return true;
    }
    return charClassTest(ranges, range_count, c);
  }

  constexpr void addChar(unsigned char c) noexcept {
    if (accepts_all || range_count >= kMaxRanges) {
      accepts_all = true;
      return;
    }
    // Sorted insert with merge
    CharRange nr = {c, c};
    CharRange tmp[kMaxRanges + 1];
    int pos = 0;
    bool inserted = false;
    for (int i = 0; i < range_count; ++i) {
      if (!inserted && c <= ranges[i].lo) {
        tmp[pos++] = nr;
        inserted = true;
      }
      tmp[pos++] = ranges[i];
    }
    if (!inserted) {
      tmp[pos++] = nr;
    }
    // Merge overlapping/adjacent
    int out = 0;
    for (int i = 0; i < pos; ++i) {
      if (out > 0 && tmp[i].lo <= tmp[out - 1].hi + 1) {
        if (tmp[i].hi > tmp[out - 1].hi) {
          tmp[out - 1].hi = tmp[i].hi;
        }
      } else {
        tmp[out++] = tmp[i];
      }
    }
    if (out > kMaxRanges) {
      accepts_all = true;
      return;
    }
    for (int i = 0; i < out; ++i) {
      ranges[i] = tmp[i];
    }
    range_count = out;
  }

  constexpr void mergeFrom(const CharRange* other, int otherCount) noexcept {
    if (accepts_all) {
      return;
    }
    for (int i = 0; i < otherCount; ++i) {
      // For multi-char ranges, just add the whole range
      for (unsigned char c = other[i].lo;; ++c) {
        addChar(c);
        if (accepts_all || c == other[i].hi) {
          break;
        }
      }
    }
  }
};

// Factory functions returning CharRangeSet for common shorthand classes.

constexpr CharRangeSet makeDigitRanges() noexcept {
  CharRangeSet rs;
  rs.addRange('0', '9');
  return rs;
}

constexpr CharRangeSet makeWordRanges() noexcept {
  CharRangeSet rs;
  rs.addRange('0', '9');
  rs.addRange('A', 'Z');
  rs.addRange('_', '_');
  rs.addRange('a', 'z');
  return rs;
}

constexpr CharRangeSet makeSpaceRanges() noexcept {
  CharRangeSet rs;
  rs.addRange('\t', '\r');
  rs.addChar(' ');
  return rs;
}

constexpr CharRangeSet makeDotRanges() noexcept {
  CharRangeSet rs;
  rs.addRange(0, 9);
  rs.addRange(11, 255);
  return rs;
}

constexpr bool addPosixClass(CharRangeSet& rs, std::string_view name) noexcept {
  if (name == "alpha") {
    rs.addRange('A', 'Z');
    rs.addRange('a', 'z');
  } else if (name == "digit") {
    rs.addRange('0', '9');
  } else if (name == "alnum") {
    rs.addRange('0', '9');
    rs.addRange('A', 'Z');
    rs.addRange('a', 'z');
  } else if (name == "upper") {
    rs.addRange('A', 'Z');
  } else if (name == "lower") {
    rs.addRange('a', 'z');
  } else if (name == "space") {
    rs.addRange('\t', '\r');
    rs.addChar(' ');
  } else if (name == "blank") {
    rs.addChar('\t');
    rs.addChar(' ');
  } else if (name == "punct") {
    rs.addRange(33, 47);
    rs.addRange(58, 64);
    rs.addRange(91, 96);
    rs.addRange(123, 126);
  } else if (name == "cntrl") {
    rs.addRange(0, 31);
    rs.addChar(127);
  } else if (name == "graph") {
    rs.addRange(33, 126);
  } else if (name == "print") {
    rs.addRange(32, 126);
  } else if (name == "xdigit") {
    rs.addRange('0', '9');
    rs.addRange('A', 'F');
    rs.addRange('a', 'f');
  } else if (name == "ascii") {
    rs.addRange(0, 127);
  } else {
    return false;
  }
  return true;
}

// Check if character class A (given as sorted non-overlapping ranges) is a
// subset of character class B. O(n+m) merge-walk since both are sorted.
constexpr bool charClassIsSubsetOf(
    const CharRange* aRanges,
    int aCount,
    const CharRange* bRanges,
    int bCount) noexcept {
  int bi = 0;
  for (int ai = 0; ai < aCount; ++ai) {
    while (bi < bCount && bRanges[bi].hi < aRanges[ai].lo) {
      ++bi;
    }
    if (bi >= bCount) {
      return false;
    }
    if (bRanges[bi].lo > aRanges[ai].lo || bRanges[bi].hi < aRanges[ai].hi) {
      return false;
    }
  }
  return true;
}

// Convenience overload using CharClassEntry indices from the AST.
constexpr bool charClassIsSubsetOf(
    const auto& ast, int ccIdxA, int ccIdxB) noexcept {
  const auto& a = ast.char_classes[ccIdxA];
  const auto& b = ast.char_classes[ccIdxB];
  return charClassIsSubsetOf(
      ast.ranges + a.range_offset,
      a.range_count,
      ast.ranges + b.range_offset,
      b.range_count);
}

} // namespace detail
} // namespace regex
} // namespace folly
