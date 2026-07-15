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

namespace folly::regex::detail {

constexpr bool isDigit(char c) noexcept {
  return '0' <= c && c <= '9';
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

// Encode a Unicode codepoint as UTF-8. Writes 1-4 bytes to `out` and
// returns the number of bytes written. Caller must ensure cp <= 0x10FFFF.
constexpr int encodeUtf8(uint32_t cp, char* out) noexcept {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  } else if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  } else {
    out[0] = static_cast<char>(0xF0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
  }
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
  int lo = 0, hi = count - 1;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    if (uc < ranges[mid].lo) {
      hi = mid - 1;
    } else if (uc > ranges[mid].hi) {
      lo = mid + 1;
    } else {
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

// Test whether character `c` is set in the compact bitmap `words` of
// `word_count` 64-bit words starting at offset `lo`.
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

  constexpr int count() const noexcept { return total_count_; }

  // Materialize the complement ranges: walk the sorted ranges and emit gaps.
  constexpr void invert() noexcept {
    CharRange sorted[256];
    int n = 0;
    {
      const CharRangeBlock* b = &first_;
      while (b) {
        for (int i = 0; i < b->count; ++i) {
          if (n < 256) {
            sorted[n++] = b->ranges[i];
          }
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

// Compact, rapidly-testable set of characters that MIGHT start a match.
// Used by literal prefix stripping and other optimizer passes. When
// `accepts_all` is true the filter trivially accepts any character.
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

  constexpr void addChar(unsigned char c) noexcept { addRange(c, c); }

  constexpr void addRange(unsigned char lo, unsigned char hi) noexcept {
    if (accepts_all) {
      return;
    }
    // Sorted insert with merge
    CharRange nr = {lo, hi};
    CharRange tmp[kMaxRanges + 1];
    int pos = 0;
    bool inserted = false;
    for (int i = 0; i < range_count; ++i) {
      if (!inserted && lo <= ranges[i].lo) {
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
    for (int i = 0; i < otherCount; ++i) {
      addRange(other[i].lo, other[i].hi);
      if (accepts_all) {
        return;
      }
    }
  }
};

// Test whether every range of aRanges is fully contained in some range of
// bRanges. Assumes both arrays are sorted non-overlapping range sets.
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

// Returns the lowercase form of an ASCII uppercase letter, or `c` unchanged.
// Used by case-insensitive backreference matching in BacktrackExecutor.
constexpr char asciiToLower(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c | 0x20) : c;
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

// Check if two sorted, non-overlapping CharRange arrays are disjoint.
// O(n+m) merge-walk: advance whichever pointer points to the range that
// ends first; if neither is strictly before the other, they overlap.
constexpr bool areRangeSetsDisjoint(
    const CharRange* a, int aCount, const CharRange* b, int bCount) noexcept {
  int i = 0;
  int j = 0;
  while (i < aCount && j < bCount) {
    if (a[i].hi < b[j].lo) {
      ++i;
    } else if (b[j].hi < a[i].lo) {
      ++j;
    } else {
      return false;
    }
  }
  return true;
}

// Add a POSIX character class to `rs`. When `caseInsensitive` is true,
// each range that overlaps an ASCII alpha range also adds the case-flipped
// equivalent — matching Perl's [:upper:]/[:lower:] semantics under /i.
// Verified against Perl: [[:upper:]] with /i matches lowercase 'a'.
constexpr bool addPosixClass(
    CharRangeSet& rs,
    std::string_view name,
    bool caseInsensitive = false) noexcept {
  if (name.empty()) {
    return false;
  }
  // Helper to add a range and its case-flipped overlap when CI is on.
  auto add = [&](unsigned char lo, unsigned char hi) {
    rs.addRange(lo, hi);
    if (caseInsensitive) {
      // Overlap with [A-Z] (65-90) → add lowercase equivalent.
      if (lo <= 'Z' && hi >= 'A') {
        unsigned char a = lo > 'A' ? lo : 'A';
        unsigned char b = hi < 'Z' ? hi : 'Z';
        rs.addRange(
            static_cast<unsigned char>(a | 0x20),
            static_cast<unsigned char>(b | 0x20));
      }
      // Overlap with [a-z] (97-122) → add uppercase equivalent.
      if (lo <= 'z' && hi >= 'a') {
        unsigned char a = lo > 'a' ? lo : 'a';
        unsigned char b = hi < 'z' ? hi : 'z';
        rs.addRange(
            static_cast<unsigned char>(a & ~0x20),
            static_cast<unsigned char>(b & ~0x20));
      }
    }
  };
  // Dispatch on the first character so we only do the few string comparisons
  // that can possibly match. The switch can lower to a jump table at
  // compile-time interpretation.
  switch (name[0]) {
    case 'a':
      if (name == "alpha") {
        add('A', 'Z');
        add('a', 'z');
        return true;
      }
      if (name == "alnum") {
        add('0', '9');
        add('A', 'Z');
        add('a', 'z');
        return true;
      }
      if (name == "ascii") {
        add(0, 127);
        return true;
      }
      return false;
    case 'b':
      if (name == "blank") {
        rs.addChar('\t');
        rs.addChar(' ');
        return true;
      }
      return false;
    case 'c':
      if (name == "cntrl") {
        add(0, 31);
        rs.addChar(127);
        return true;
      }
      return false;
    case 'd':
      if (name == "digit") {
        add('0', '9');
        return true;
      }
      return false;
    case 'g':
      if (name == "graph") {
        add(33, 126);
        return true;
      }
      return false;
    case 'l':
      if (name == "lower") {
        add('a', 'z');
        return true;
      }
      return false;
    case 'p':
      if (name == "punct") {
        add(33, 47);
        add(58, 64);
        add(91, 96);
        add(123, 126);
        return true;
      }
      if (name == "print") {
        add(32, 126);
        return true;
      }
      return false;
    case 's':
      if (name == "space") {
        add('\t', '\r');
        rs.addChar(' ');
        return true;
      }
      return false;
    case 'u':
      if (name == "upper") {
        add('A', 'Z');
        return true;
      }
      return false;
    case 'w':
      // Perl/PCRE2 extension: equivalent to \w (alphanumeric + '_').
      if (name == "word") {
        add('0', '9');
        add('A', 'Z');
        rs.addChar('_');
        add('a', 'z');
        return true;
      }
      return false;
    case 'x':
      if (name == "xdigit") {
        add('0', '9');
        add('A', 'F');
        add('a', 'f');
        return true;
      }
      return false;
    default:
      return false;
  }
}

} // namespace folly::regex::detail
