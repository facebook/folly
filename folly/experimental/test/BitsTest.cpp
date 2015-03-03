/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/experimental/Bits.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;

template <class T>
void runSimpleTest8() {
  auto load = detail::BitsTraits<T>::load;

  EXPECT_EQ(0, Bits<T>::blockCount(0));
  EXPECT_EQ(1, Bits<T>::blockCount(1));
  EXPECT_EQ(1, Bits<T>::blockCount(8));
  EXPECT_EQ(2, Bits<T>::blockCount(9));
  EXPECT_EQ(256, Bits<T>::blockCount(2048));
  EXPECT_EQ(257, Bits<T>::blockCount(2049));

  EXPECT_EQ(4, Bits<T>::blockIndex(39));
  EXPECT_EQ(7, Bits<T>::bitOffset(39));
  EXPECT_EQ(5, Bits<T>::blockIndex(40));
  EXPECT_EQ(0, Bits<T>::bitOffset(40));

  T buf[256];
  std::fill(buf, buf + 256, T(0));

  Bits<T>::set(buf, 36);
  Bits<T>::set(buf, 39);
  EXPECT_EQ((1 << 7) | (1 << 4), load(buf[4]));
  EXPECT_EQ(0, load(buf[5]));
  Bits<T>::clear(buf, 39);
  EXPECT_EQ(1 << 4, load(buf[4]));
  EXPECT_EQ(0, load(buf[5]));
  Bits<T>::set(buf, 40);
  EXPECT_EQ(1 << 4, load(buf[4]));
  EXPECT_EQ(1, load(buf[5]));

  EXPECT_EQ(2, Bits<T>::count(buf, buf + 256));
}

TEST(Bits, Simple8) {
  runSimpleTest8<uint8_t>();
}

TEST(Bits, SimpleUnaligned8) {
  runSimpleTest8<Unaligned<uint8_t>>();
}

template <class T>
void runSimpleTest64() {
  auto load = detail::BitsTraits<T>::load;

  EXPECT_EQ(0, Bits<T>::blockCount(0));
  EXPECT_EQ(1, Bits<T>::blockCount(1));
  EXPECT_EQ(1, Bits<T>::blockCount(8));
  EXPECT_EQ(1, Bits<T>::blockCount(9));
  EXPECT_EQ(1, Bits<T>::blockCount(64));
  EXPECT_EQ(2, Bits<T>::blockCount(65));
  EXPECT_EQ(32, Bits<T>::blockCount(2048));
  EXPECT_EQ(33, Bits<T>::blockCount(2049));

  EXPECT_EQ(0, Bits<T>::blockIndex(39));
  EXPECT_EQ(39, Bits<T>::bitOffset(39));
  EXPECT_EQ(4, Bits<T>::blockIndex(319));
  EXPECT_EQ(63, Bits<T>::bitOffset(319));
  EXPECT_EQ(5, Bits<T>::blockIndex(320));
  EXPECT_EQ(0, Bits<T>::bitOffset(320));

  T buf[256];
  std::fill(buf, buf + 256, T(0));

  Bits<T>::set(buf, 300);
  Bits<T>::set(buf, 319);
  EXPECT_EQ((uint64_t(1) << 44) | (uint64_t(1) << 63), load(buf[4]));
  EXPECT_EQ(0, load(buf[5]));
  Bits<T>::clear(buf, 319);
  EXPECT_EQ(uint64_t(1) << 44, load(buf[4]));
  EXPECT_EQ(0, load(buf[5]));
  Bits<T>::set(buf, 320);
  EXPECT_EQ(uint64_t(1) << 44, load(buf[4]));
  EXPECT_EQ(1, load(buf[5]));

  EXPECT_EQ(2, Bits<T>::count(buf, buf + 256));
}

TEST(Bits, Simple64) {
  runSimpleTest64<uint64_t>();
}

TEST(Bits, SimpleUnaligned64) {
  runSimpleTest64<Unaligned<uint64_t>>();
}

template <class T>
void runMultiBitTest8() {
  auto load = detail::BitsTraits<T>::load;
  T buf[] = {0x12, 0x34, 0x56, 0x78};

  EXPECT_EQ(0x02, load(Bits<T>::get(buf, 0, 4)));
  EXPECT_EQ(0x1a, load(Bits<T>::get(buf, 9, 5)));
  EXPECT_EQ(0xb1, load(Bits<T>::get(buf, 13, 8)));

  Bits<T>::set(buf, 0, 4, 0x0b);
  EXPECT_EQ(0x1b, load(buf[0]));
  EXPECT_EQ(0x34, load(buf[1]));
  EXPECT_EQ(0x56, load(buf[2]));
  EXPECT_EQ(0x78, load(buf[3]));

  Bits<T>::set(buf, 9, 5, 0x0e);
  EXPECT_EQ(0x1b, load(buf[0]));
  EXPECT_EQ(0x1c, load(buf[1]));
  EXPECT_EQ(0x56, load(buf[2]));
  EXPECT_EQ(0x78, load(buf[3]));

  Bits<T>::set(buf, 13, 8, 0xaa);
  EXPECT_EQ(0x1b, load(buf[0]));
  EXPECT_EQ(0x5c, load(buf[1]));
  EXPECT_EQ(0x55, load(buf[2]));
  EXPECT_EQ(0x78, load(buf[3]));
}

TEST(Bits, MultiBit8) {
  runMultiBitTest8<uint8_t>();
}

TEST(Bits, MultiBitUnaligned8) {
  runMultiBitTest8<Unaligned<uint8_t>>();
}

template <class T>
void runSignedMultiBitTest8() {
  auto load = detail::BitsTraits<T>::load;
  T buf[] = {0x12, 0x34, 0x56, 0x78};

  EXPECT_EQ(0x02, load(Bits<T>::get(buf, 0, 4)));
  EXPECT_EQ(0x1a-32, load(Bits<T>::get(buf, 9, 5)));
  EXPECT_EQ(0xb1-256, load(Bits<T>::get(buf, 13, 8)));

  Bits<T>::set(buf, 0, 4, 0x0b - 0x10);
  EXPECT_EQ(0x1b, load(buf[0]));
  EXPECT_EQ(0x34, load(buf[1]));
  EXPECT_EQ(0x56, load(buf[2]));
  EXPECT_EQ(0x78, load(buf[3]));

  Bits<T>::set(buf, 9, 5, 0x0e);
  EXPECT_EQ(0x1b, load(buf[0]));
  EXPECT_EQ(0x1c, load(buf[1]));
  EXPECT_EQ(0x56, load(buf[2]));
  EXPECT_EQ(0x78, load(buf[3]));

  Bits<T>::set(buf, 13, 8, 0xaa - 0x100);
  EXPECT_EQ(0x1b, load(buf[0]));
  EXPECT_EQ(0x5c, load(buf[1]));
  EXPECT_EQ(0x55, load(buf[2]));
  EXPECT_EQ(0x78, load(buf[3]));
}


TEST(Bits, SignedMultiBit8) {
  runSignedMultiBitTest8<int8_t>();
}

template <class T, class R = T>
void runMultiBitTest64() {
  auto load = detail::BitsTraits<T>::load;
  T buf[] = {0x123456789abcdef0, 0x13579bdf2468ace0};

  EXPECT_EQ(0x123456789abcdef0, load(Bits<T>::get(buf, 0, 64)));
  EXPECT_EQ(0xf0, load(Bits<T>::get(buf, 0, 8)));
  EXPECT_EQ(0x89abcdef, load(Bits<T>::get(buf, 4, 32)));
  EXPECT_EQ(0x189abcdef, load(Bits<T>::get(buf, 4, 33)));

  Bits<T>::set(buf, 4, 31, 0x55555555);
  EXPECT_EQ(0xd5555555, load(Bits<T>::get(buf, 4, 32)));
  EXPECT_EQ(0x1d5555555, load(Bits<T>::get(buf, 4, 33)));
  EXPECT_EQ(0xd55555550, load(Bits<T>::get(buf, 0, 36)));

  Bits<T>::set(buf, 0, 64, 0x23456789abcdef01);
  EXPECT_EQ(0x23456789abcdef01, load(Bits<T>::get(buf, 0, 64)));
}

TEST(Bits, MultiBit64) {
  runMultiBitTest64<uint64_t>();
}

TEST(Bits, MultiBitSigned64) {
  //runMultiBitTest64<int64_t>();
}

TEST(Bits, MultiBitUnaligned64) {
  runMultiBitTest64<Unaligned<uint64_t>, uint64_t>();
}

namespace {
template <bool aligned, class T>
typename std::enable_if<!aligned>::type testSet(uint8_t *buf,
                                                size_t start,
                                                size_t bits,
                                                T value) {
  Bits<Unaligned<T>>::set(
      reinterpret_cast<Unaligned<T> *>(buf), start, bits, value);
}

template <bool aligned, class T>
typename std::enable_if<aligned>::type testSet(uint8_t *buf,
                                               size_t start,
                                               size_t bits,
                                               T value) {
  Bits<T>::set(reinterpret_cast<T *>(buf), start, bits, value);
}

template <bool aligned, class T>
typename std::enable_if<!aligned, T>::type testGet(uint8_t *buf,
                                                   size_t start,
                                                   size_t bits) {
  return Bits<Unaligned<T>>::get(
      reinterpret_cast<Unaligned<T> *>(buf), start, bits);
}

template <bool aligned, class T>
typename std::enable_if<aligned, T>::type testGet(uint8_t *buf,
                                                  size_t start,
                                                  size_t bits) {
  return Bits<T>::get(reinterpret_cast<T *>(buf), start, bits);
}

template <class T, bool negate = false>
T testValue(int bits) {
  if (std::is_signed<T>::value) {
    --bits;
  }
  auto value = pow(2, bits) * (negate ? -2.0 : 2.0) / 3.0;
  CHECK_GE(value, std::numeric_limits<T>::min());
  CHECK_LE(value, std::numeric_limits<T>::max());
  return static_cast<T>(value);
}
}

template <bool aligned>
void testConcatenation() {
  // concatenate fields of length 1, 2, 3, ... 64, all unsigned, storing 2/3s
  // the maximum value in each.
#define EACH_UNSIGNED_SIZE(MACRO, ARG) \
  MACRO(8, uint8_t, ARG)               \
  MACRO(16, uint16_t, ARG)             \
  MACRO(32, uint32_t, ARG)             \
  MACRO(64, uint64_t, ARG)
#define EACH_SIGNED_SIZE(MACRO, ARG) \
  MACRO(7, int8_t, ARG)              \
  MACRO(15, int16_t, ARG)            \
  MACRO(31, int32_t, ARG)            \
  MACRO(63, int64_t, ARG)
  // calculate how much buffer size we need
  size_t bufSize = 0;
  {
    size_t w = 0;
#define SIZE_TEST(N, T, NEG)        \
  for (size_t s = 0; s <= N; ++s) { \
    w += s;                         \
  }
    EACH_UNSIGNED_SIZE(SIZE_TEST, false)
    EACH_SIGNED_SIZE(SIZE_TEST, false)
    EACH_SIGNED_SIZE(SIZE_TEST, true)
#undef SIZE_TEST
    bufSize = w;
  }
  // bits->bytes, rounding up
  bufSize = (bufSize + 7) / 8;
  // round up to next multiple of 8
  bufSize = (bufSize + 7) / 8 * 8;
  std::vector<uint8_t> buffer(bufSize);
  uint8_t *buf = buffer.data();
  {
    size_t w = 0;
#define WRITE_TEST(N, T, NEG)                                                 \
  for (size_t s = 0; s <= N; ++s) {                                           \
    CHECK_LE(s + w, 8 * bufSize);                                             \
    testSet<aligned>(buf, w, s, testValue<T, NEG>(s));                        \
    EXPECT_EQ((testValue<T, NEG>(s)), (testGet<aligned, T>(buf, w, s))) << s; \
    w += s;                                                                   \
  }
    EACH_UNSIGNED_SIZE(WRITE_TEST, false)
    EACH_SIGNED_SIZE(WRITE_TEST, false)
    EACH_SIGNED_SIZE(WRITE_TEST, true)
#undef WRITE_TEST
  }
  {
    size_t r = 0;
#define READ_TEST(N, T, NEG)                                                  \
  for (size_t s = 0; s <= N; ++s) {                                           \
    CHECK_LE(s + r, 8 * bufSize);                                             \
    EXPECT_EQ((testValue<T, NEG>(s)), (testGet<aligned, T>(buf, r, s))) << s; \
    r += s;                                                                   \
  }
    EACH_UNSIGNED_SIZE(READ_TEST, false)
    EACH_SIGNED_SIZE(READ_TEST, false)
    EACH_SIGNED_SIZE(READ_TEST, true)
#undef READ_TEST
  }
#undef EACH_UNSIGNED_SIZE
}

TEST(Bits, ConcatenationUnalignedUnsigned) { testConcatenation<false>(); }

TEST(Bits, ConcatenationAligned) { testConcatenation<true>(); }

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
