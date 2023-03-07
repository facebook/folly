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

#include <folly/detail/SimdForEach.h>

#include <folly/portability/GTest.h>

namespace folly {
namespace simd_detail {

constexpr int kCardinal = 4;

struct TestDelegate {
  char* stopAt = nullptr;

  bool operator()(char* s, ignore_extrema ignore) const {
    int middle = kCardinal - ignore.first - ignore.last;
    while (ignore.first--) {
      *s++ = 'i';
    }
    while (middle--) {
      *s++ = 'o';
    }
    while (ignore.last--) {
      *s++ = 'i';
    }

    return stopAt != nullptr && s > stopAt;
  }

  bool operator()(char* s, ignore_none) const {
    for (int i = 0; i != kCardinal; ++i) {
      *s++ = 'o';
    }
    return stopAt != nullptr && s > stopAt;
  }
};

std::string run(int offset, int len, int stopAt) {
  alignas(64) std::array<char, 100u> buf;
  buf.fill(0);

  TestDelegate delegate{stopAt == -1 ? nullptr : buf.data() + stopAt};
  simdForEachAligning(
      kCardinal, buf.data() + offset, buf.data() + offset + len, delegate);
  return std::string(buf.data());
}

TEST(SimdForEachAligningTest, Tails) {
  ASSERT_EQ("", run(0, 0, -1));
  ASSERT_EQ("", run(1, 0, -1));
  ASSERT_EQ("", run(2, 0, -1));
  ASSERT_EQ("", run(3, 0, -1));

  ASSERT_EQ("oiii", run(0, 1, -1));
  ASSERT_EQ("ioii", run(1, 1, -1));
  ASSERT_EQ("iioi", run(2, 1, -1));
  ASSERT_EQ("iiio", run(3, 1, -1));

  ASSERT_EQ("ooii", run(0, 2, -1));
  ASSERT_EQ("iooi", run(1, 2, -1));
  ASSERT_EQ("iioo", run(2, 2, -1));
  ASSERT_EQ("iiiooiii", run(3, 2, -1));

  ASSERT_EQ("oooi", run(0, 3, -1));
  ASSERT_EQ("iooo", run(1, 3, -1));
  ASSERT_EQ("iioooiii", run(2, 3, -1));
  ASSERT_EQ("iiioooii", run(3, 3, -1));

  ASSERT_EQ("oooo", run(0, 4, -1));
  ASSERT_EQ("iooooiii", run(1, 4, -1));
  ASSERT_EQ("iiooooii", run(2, 4, -1));
  ASSERT_EQ("iiiooooi", run(3, 4, -1));

  ASSERT_EQ("oooooiii", run(0, 5, -1));
  ASSERT_EQ("ioooooii", run(1, 5, -1));
  ASSERT_EQ("iioooooi", run(2, 5, -1));
  ASSERT_EQ("iiiooooo", run(3, 5, -1));
}

TEST(SimdForEachAligningTest, Large) {
  ASSERT_EQ(
      "oooo"
      "oooo"
      "oooo"
      "oooo"
      "ooii",
      run(0, 18, -1));
  ASSERT_EQ(
      "iooo"
      "oooo"
      "oooo"
      "oooo"
      "oooi",
      run(1, 18, -1));
  ASSERT_EQ(
      "iioo"
      "oooo"
      "oooo"
      "oooo"
      "oooo",
      run(2, 18, -1));
  ASSERT_EQ(
      "iiio"
      "oooo"
      "oooo"
      "oooo"
      "oooo"
      "oiii",
      run(3, 18, -1));
}

TEST(SimdForEachAligningTest, Stops) {
  for (int i = 0; i != 4; ++i) {
    ASSERT_EQ("oooo", run(0, 18, i));
  }
  for (int i = 0; i != 4; ++i) {
    ASSERT_EQ(
        "oooo"
        "oooo",
        run(0, 18, 4 + i));
  }
  for (int i = 0; i != 4; ++i) {
    ASSERT_EQ(
        "oooo"
        "oooo"
        "oooo"
        "oooo"
        "ooii",
        run(0, 18, 16 + i));
  }
}

} // namespace simd_detail
} // namespace folly
