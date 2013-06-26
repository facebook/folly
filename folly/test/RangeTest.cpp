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

// @author Kristina Holst (kholst@fb.com)
// @author Andrei Alexandrescu (andrei.alexandrescu@fb.com)

#include "folly/Range.h"

#include <limits>
#include <stdlib.h>
#include <string>
#include <sys/mman.h>
#include <boost/range/concepts.hpp>
#include <gtest/gtest.h>

namespace folly { namespace detail {

// declaration of functions in Range.cpp
size_t qfind_first_byte_of_memchr(const StringPiece& haystack,
                                  const StringPiece& needles);

size_t qfind_first_byte_of_byteset(const StringPiece& haystack,
                                   const StringPiece& needles);

}}  // namespaces

using namespace folly;
using namespace std;

BOOST_CONCEPT_ASSERT((boost::RandomAccessRangeConcept<StringPiece>));

TEST(StringPiece, All) {
  const char* foo = "foo";
  const char* foo2 = "foo";
  string fooStr(foo);
  string foo2Str(foo2);

  // we expect the compiler to optimize things so that there's only one copy
  // of the string literal "foo", even though we've got it in multiple places
  EXPECT_EQ(foo, foo2);  // remember, this uses ==, not strcmp, so it's a ptr
                         // comparison rather than lexical

  // the string object creates copies though, so the c_str of these should be
  // distinct
  EXPECT_NE(fooStr.c_str(), foo2Str.c_str());

  // test the basic StringPiece functionality
  StringPiece s(foo);
  EXPECT_EQ(s.size(), 3);

  EXPECT_EQ(s.start(), foo);              // ptr comparison
  EXPECT_NE(s.start(), fooStr.c_str());   // ptr comparison
  EXPECT_NE(s.start(), foo2Str.c_str());  // ptr comparison

  EXPECT_EQ(s.toString(), foo);              // lexical comparison
  EXPECT_EQ(s.toString(), fooStr.c_str());   // lexical comparison
  EXPECT_EQ(s.toString(), foo2Str.c_str());  // lexical comparison

  EXPECT_EQ(s, foo);                      // lexical comparison
  EXPECT_EQ(s, fooStr);                   // lexical comparison
  EXPECT_EQ(s, foo2Str);                  // lexical comparison
  EXPECT_EQ(foo, s);

  // check using StringPiece to reference substrings
  const char* foobarbaz = "foobarbaz";

  // the full "foobarbaz"
  s.reset(foobarbaz, strlen(foobarbaz));
  EXPECT_EQ(s.size(), 9);
  EXPECT_EQ(s.start(), foobarbaz);
  EXPECT_EQ(s, "foobarbaz");

  // only the 'foo'
  s.assign(foobarbaz, foobarbaz + 3);
  EXPECT_EQ(s.size(), 3);
  EXPECT_EQ(s.start(), foobarbaz);
  EXPECT_EQ(s, "foo");

  // find
  s.reset(foobarbaz, strlen(foobarbaz));
  EXPECT_EQ(s.find("bar"), 3);
  EXPECT_EQ(s.find("ba", 3), 3);
  EXPECT_EQ(s.find("ba", 4), 6);
  EXPECT_EQ(s.find("notfound"), StringPiece::npos);
  EXPECT_EQ(s.find("notfound", 1), StringPiece::npos);
  EXPECT_EQ(s.find("bar", 4), StringPiece::npos);  // starting position too far
  // starting pos that is obviously past the end -- This works for std::string
  EXPECT_EQ(s.toString().find("notfound", 55), StringPiece::npos);
  EXPECT_EQ(s.find("z", s.size()), StringPiece::npos);
  EXPECT_EQ(s.find("z", 55), StringPiece::npos);
  // empty needle
  EXPECT_EQ(s.find(""), std::string().find(""));
  EXPECT_EQ(s.find(""), 0);

  // single char finds
  EXPECT_EQ(s.find('b'), 3);
  EXPECT_EQ(s.find('b', 3), 3);
  EXPECT_EQ(s.find('b', 4), 6);
  EXPECT_EQ(s.find('o', 2), 2);
  EXPECT_EQ(s.find('y'), StringPiece::npos);
  EXPECT_EQ(s.find('y', 1), StringPiece::npos);
  EXPECT_EQ(s.find('o', 4), StringPiece::npos);  // starting position too far
  // starting pos that is obviously past the end -- This works for std::string
  EXPECT_EQ(s.toString().find('y', 55), StringPiece::npos);
  EXPECT_EQ(s.find('z', s.size()), StringPiece::npos);
  EXPECT_EQ(s.find('z', 55), StringPiece::npos);
  // null char
  EXPECT_EQ(s.find('\0'), std::string().find('\0'));
  EXPECT_EQ(s.find('\0'), StringPiece::npos);

  // single char rfinds
  EXPECT_EQ(s.rfind('b'), 6);
  EXPECT_EQ(s.rfind('y'), StringPiece::npos);
  EXPECT_EQ(s.str().rfind('y'), StringPiece::npos);
  EXPECT_EQ(ByteRange(s).rfind('b'), 6);
  EXPECT_EQ(ByteRange(s).rfind('y'), StringPiece::npos);
  // null char
  EXPECT_EQ(s.rfind('\0'), s.str().rfind('\0'));
  EXPECT_EQ(s.rfind('\0'), StringPiece::npos);

  // find_first_of
  s.reset(foobarbaz, strlen(foobarbaz));
  EXPECT_EQ(s.find_first_of("bar"), 3);
  EXPECT_EQ(s.find_first_of("ba", 3), 3);
  EXPECT_EQ(s.find_first_of("ba", 4), 4);
  EXPECT_EQ(s.find_first_of("xyxy"), StringPiece::npos);
  EXPECT_EQ(s.find_first_of("xyxy", 1), StringPiece::npos);
  // starting position too far
  EXPECT_EQ(s.find_first_of("foo", 4), StringPiece::npos);
  // starting pos that is obviously past the end -- This works for std::string
  EXPECT_EQ(s.toString().find_first_of("xyxy", 55), StringPiece::npos);
  EXPECT_EQ(s.find_first_of("z", s.size()), StringPiece::npos);
  EXPECT_EQ(s.find_first_of("z", 55), StringPiece::npos);
  // empty needle. Note that this returns npos, while find() returns 0!
  EXPECT_EQ(s.find_first_of(""), std::string().find_first_of(""));
  EXPECT_EQ(s.find_first_of(""), StringPiece::npos);

  // single char find_first_ofs
  EXPECT_EQ(s.find_first_of('b'), 3);
  EXPECT_EQ(s.find_first_of('b', 3), 3);
  EXPECT_EQ(s.find_first_of('b', 4), 6);
  EXPECT_EQ(s.find_first_of('o', 2), 2);
  EXPECT_EQ(s.find_first_of('y'), StringPiece::npos);
  EXPECT_EQ(s.find_first_of('y', 1), StringPiece::npos);
  // starting position too far
  EXPECT_EQ(s.find_first_of('o', 4), StringPiece::npos);
  // starting pos that is obviously past the end -- This works for std::string
  EXPECT_EQ(s.toString().find_first_of('y', 55), StringPiece::npos);
  EXPECT_EQ(s.find_first_of('z', s.size()), StringPiece::npos);
  EXPECT_EQ(s.find_first_of('z', 55), StringPiece::npos);
  // null char
  EXPECT_EQ(s.find_first_of('\0'), std::string().find_first_of('\0'));
  EXPECT_EQ(s.find_first_of('\0'), StringPiece::npos);

  // just "barbaz"
  s.reset(foobarbaz + 3, strlen(foobarbaz + 3));
  EXPECT_EQ(s.size(), 6);
  EXPECT_EQ(s.start(), foobarbaz + 3);
  EXPECT_EQ(s, "barbaz");

  // just "bar"
  s.reset(foobarbaz + 3, 3);
  EXPECT_EQ(s.size(), 3);
  EXPECT_EQ(s, "bar");

  // clear
  s.clear();
  EXPECT_EQ(s.toString(), "");

  // test an empty StringPiece
  StringPiece s2;
  EXPECT_EQ(s2.size(), 0);

  // Test comparison operators
  foo = "";
  EXPECT_LE(s, foo);
  EXPECT_LE(foo, s);
  EXPECT_GE(s, foo);
  EXPECT_GE(foo, s);
  EXPECT_EQ(s, foo);
  EXPECT_EQ(foo, s);

  foo = "abc";
  EXPECT_LE(s, foo);
  EXPECT_LT(s, foo);
  EXPECT_GE(foo, s);
  EXPECT_GT(foo, s);
  EXPECT_NE(s, foo);

  EXPECT_LE(s, s);
  EXPECT_LE(s, s);
  EXPECT_GE(s, s);
  EXPECT_GE(s, s);
  EXPECT_EQ(s, s);
  EXPECT_EQ(s, s);

  s = "abc";
  s2 = "abc";
  EXPECT_LE(s, s2);
  EXPECT_LE(s2, s);
  EXPECT_GE(s, s2);
  EXPECT_GE(s2, s);
  EXPECT_EQ(s, s2);
  EXPECT_EQ(s2, s);
}

template <class T>
void expectLT(const T& a, const T& b) {
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(a <= b);
  EXPECT_FALSE(a == b);
  EXPECT_FALSE(a >= b);
  EXPECT_FALSE(a > b);

  EXPECT_FALSE(b < a);
  EXPECT_FALSE(b <= a);
  EXPECT_TRUE(b >= a);
  EXPECT_TRUE(b > a);
}

template <class T>
void expectEQ(const T& a, const T& b) {
  EXPECT_FALSE(a < b);
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(a == b);
  EXPECT_TRUE(a >= b);
  EXPECT_FALSE(a > b);
}

TEST(StringPiece, EightBitComparisons) {
  char values[] = {'\x00', '\x20', '\x40', '\x7f', '\x80', '\xc0', '\xff'};
  constexpr size_t count = sizeof(values) / sizeof(values[0]);
  for (size_t i = 0; i < count; ++i) {
    std::string a(1, values[i]);
    // Defeat copy-on-write
    std::string aCopy(a.data(), a.size());
    expectEQ(a, aCopy);
    expectEQ(StringPiece(a), StringPiece(aCopy));

    for (size_t j = i + 1; j < count; ++j) {
      std::string b(1, values[j]);
      expectLT(a, b);
      expectLT(StringPiece(a), StringPiece(b));
    }
  }
}

TEST(StringPiece, ToByteRange) {
  StringPiece a("hello");
  ByteRange b(a);
  EXPECT_EQ(static_cast<const void*>(a.begin()),
            static_cast<const void*>(b.begin()));
  EXPECT_EQ(static_cast<const void*>(a.end()),
            static_cast<const void*>(b.end()));

  // and convert back again
  StringPiece c(b);
  EXPECT_EQ(a.begin(), c.begin());
  EXPECT_EQ(a.end(), c.end());
}

TEST(StringPiece, InvalidRange) {
  StringPiece a("hello");
  EXPECT_EQ(a, a.subpiece(0, 10));
  EXPECT_EQ(StringPiece("ello"), a.subpiece(1));
  EXPECT_EQ(StringPiece("ello"), a.subpiece(1, std::string::npos));
  EXPECT_EQ(StringPiece("ell"), a.subpiece(1, 3));
  EXPECT_THROW(a.subpiece(6, 7), std::out_of_range);
  EXPECT_THROW(a.subpiece(6), std::out_of_range);

  std::string b("hello");
  EXPECT_EQ(a, StringPiece(b, 0, 10));
  EXPECT_EQ("ello", a.subpiece(1));
  EXPECT_EQ("ello", a.subpiece(1, std::string::npos));
  EXPECT_EQ("ell", a.subpiece(1, 3));
  EXPECT_THROW(a.subpiece(6, 7), std::out_of_range);
  EXPECT_THROW(a.subpiece(6), std::out_of_range);
}

template <typename NeedleFinder>
class NeedleFinderTest : public ::testing::Test {
 public:
  static size_t find_first_byte_of(StringPiece haystack, StringPiece needles) {
    return NeedleFinder::find_first_byte_of(haystack, needles);
  }
};

struct SseNeedleFinder {
  static size_t find_first_byte_of(StringPiece haystack, StringPiece needles) {
    // This will only use the SSE version if it is supported on this CPU
    // (selected using ifunc).
    return detail::qfind_first_byte_of(haystack, needles);
  }
};

struct NoSseNeedleFinder {
  static size_t find_first_byte_of(StringPiece haystack, StringPiece needles) {
    return detail::qfind_first_byte_of_nosse(haystack, needles);
  }
};

struct MemchrNeedleFinder {
  static size_t find_first_byte_of(StringPiece haystack, StringPiece needles) {
    return detail::qfind_first_byte_of_memchr(haystack, needles);
  }
};

struct ByteSetNeedleFinder {
  static size_t find_first_byte_of(StringPiece haystack, StringPiece needles) {
    return detail::qfind_first_byte_of_byteset(haystack, needles);
  }
};

typedef ::testing::Types<SseNeedleFinder, NoSseNeedleFinder, MemchrNeedleFinder,
                         ByteSetNeedleFinder> NeedleFinders;
TYPED_TEST_CASE(NeedleFinderTest, NeedleFinders);

TYPED_TEST(NeedleFinderTest, Null) {
  { // null characters in the string
    string s(10, char(0));
    s[5] = 'b';
    string delims("abc");
    EXPECT_EQ(5, this->find_first_byte_of(s, delims));
  }
  { // null characters in delim
    string s("abc");
    string delims(10, char(0));
    delims[3] = 'c';
    delims[7] = 'b';
    EXPECT_EQ(1, this->find_first_byte_of(s, delims));
  }
  { // range not terminated by null character
    string buf = "abcdefghijklmnopqrstuvwxyz";
    StringPiece s(buf.data() + 5, 3);
    StringPiece delims("z");
    EXPECT_EQ(string::npos, this->find_first_byte_of(s, delims));
  }
}

TYPED_TEST(NeedleFinderTest, DelimDuplicates) {
  string delims(1000, 'b');
  EXPECT_EQ(1, this->find_first_byte_of("abc", delims));
  EXPECT_EQ(string::npos, this->find_first_byte_of("ac", delims));
}

TYPED_TEST(NeedleFinderTest, Empty) {
  string a = "abc";
  string b = "";
  EXPECT_EQ(string::npos, this->find_first_byte_of(a, b));
  EXPECT_EQ(string::npos, this->find_first_byte_of(b, a));
  EXPECT_EQ(string::npos, this->find_first_byte_of(b, b));
}

TYPED_TEST(NeedleFinderTest, Unaligned) {
  // works correctly even if input buffers are not 16-byte aligned
  string s = "0123456789ABCDEFGH";
  for (int i = 0; i < s.size(); ++i) {
    StringPiece a(s.c_str() + i);
    for (int j = 0; j < s.size(); ++j) {
      StringPiece b(s.c_str() + j);
      EXPECT_EQ((i > j) ? 0 : j - i, this->find_first_byte_of(a, b));
    }
  }
}

// for some algorithms (specifically those that create a set of needles),
// we check for the edge-case of _all_ possible needles being sought.
TYPED_TEST(NeedleFinderTest, Needles256) {
  string needles;
  const auto minValue = std::numeric_limits<StringPiece::value_type>::min();
  const auto maxValue = std::numeric_limits<StringPiece::value_type>::max();
  // make the size ~big to avoid any edge-case branches for tiny haystacks
  const int haystackSize = 50;
  for (int i = minValue; i <= maxValue; i++) {  // <=
    needles.push_back(i);
  }
  EXPECT_EQ(StringPiece::npos, this->find_first_byte_of("", needles));
  for (int i = minValue; i <= maxValue; i++) {
    EXPECT_EQ(0, this->find_first_byte_of(string(haystackSize, i), needles));
  }

  needles.append("these are redundant characters");
  EXPECT_EQ(StringPiece::npos, this->find_first_byte_of("", needles));
  for (int i = minValue; i <= maxValue; i++) {
    EXPECT_EQ(0, this->find_first_byte_of(string(haystackSize, i), needles));
  }
}

TYPED_TEST(NeedleFinderTest, Base) {
  for (int i = 0; i < 32; ++i) {
    for (int j = 0; j < 32; ++j) {
      string s = string(i, 'X') + "abca" + string(i, 'X');
      string delims = string(j, 'Y') + "a" + string(j, 'Y');
      EXPECT_EQ(i, this->find_first_byte_of(s, delims));
    }
  }
}

const size_t kPageSize = 4096;
// Updates contents so that any read accesses past the last byte will
// cause a SIGSEGV.  It accomplishes this by changing access to the page that
// begins immediately after the end of the contents (as allocators and mmap()
// all operate on page boundaries, this is a reasonable assumption).
// This function will also initialize buf, which caller must free().
void createProtectedBuf(StringPiece& contents, char** buf) {
  ASSERT_LE(contents.size(), kPageSize);
  const size_t kSuccess = 0;
  char* tmp;
  if (kSuccess != posix_memalign((void**)buf, kPageSize, 2 * kPageSize)) {
    ASSERT_FALSE(true);
  }
  mprotect(*buf + kPageSize, kPageSize, PROT_NONE);
  size_t newBegin = kPageSize - contents.size();
  memcpy(*buf + newBegin, contents.data(), contents.size());
  contents.reset(*buf + newBegin, contents.size());
}

TYPED_TEST(NeedleFinderTest, NoSegFault) {
  const string base = string(32, 'a') + string("b");
  const string delims = string(32, 'c') + string("b");
  for (int i = 0; i <= 32; i++) {
    for (int j = 0; j <= 33; j++) {
      for (int shouldFind = 0; shouldFind <= 1; ++shouldFind) {
        StringPiece s1(base);
        s1.advance(i);
        ASSERT_TRUE(!s1.empty());
        if (!shouldFind) {
          s1.pop_back();
        }
        StringPiece s2(delims);
        s2.advance(j);
        char* buf1;
        char* buf2;
        createProtectedBuf(s1, &buf1);
        createProtectedBuf(s2, &buf2);
        // printf("s1: '%s' (%ld) \ts2: '%s' (%ld)\n",
        //        string(s1.data(), s1.size()).c_str(), s1.size(),
        //        string(s2.data(), s2.size()).c_str(), s2.size());
        auto r1 = this->find_first_byte_of(s1, s2);
        auto f1 = std::find_first_of(s1.begin(), s1.end(),
                                     s2.begin(), s2.end());
        auto e1 = (f1 == s1.end()) ? StringPiece::npos : f1 - s1.begin();
        EXPECT_EQ(r1, e1);
        auto r2 = this->find_first_byte_of(s2, s1);
        auto f2 = std::find_first_of(s2.begin(), s2.end(),
                                     s1.begin(), s1.end());
        auto e2 = (f2 == s2.end()) ? StringPiece::npos : f2 - s2.begin();
        EXPECT_EQ(r2, e2);
        free(buf1);
        free(buf2);
      }
    }
  }
}
