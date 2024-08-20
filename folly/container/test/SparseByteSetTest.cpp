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

#include <folly/container/SparseByteSet.h>

#include <cstdint>
#include <limits>
#include <random>
#include <set>

#include <folly/portability/GTest.h>

using namespace std;
using namespace folly;

namespace {

class SparseByteSetTest : public testing::Test {
 protected:
  using lims = numeric_limits<uint8_t>;
  SparseByteSet s;
};

} // namespace

TEST_F(SparseByteSetTest, empty) {
  for (auto c = lims::min(); c < lims::max(); ++c) {
    EXPECT_FALSE(s.contains(c));
  }
}

TEST_F(SparseByteSetTest, each) {
  for (auto c = lims::min(); c < lims::max(); ++c) {
    EXPECT_TRUE(s.add(c));
    EXPECT_TRUE(s.contains(c));
  }
  for (auto c = lims::min(); c < lims::max(); ++c) {
    EXPECT_FALSE(s.add(c));
    EXPECT_TRUE(s.contains(c));
  }
}

TEST_F(SparseByteSetTest, each_random) {
  mt19937 rng;
  uniform_int_distribution<uint16_t> dist{lims::min(), lims::max()};
  set<uint8_t> added;
  while (added.size() <= lims::max()) {
    auto c = uint8_t(dist(rng));
    EXPECT_EQ(added.count(c), s.contains(c));
    EXPECT_EQ(!added.count(c), s.add(c));
    added.insert(c);
    EXPECT_TRUE(added.count(c)); // sanity
    EXPECT_TRUE(s.contains(c));
  }
}

TEST_F(SparseByteSetTest, clear) {
  for (auto c = lims::min(); c < lims::max(); ++c) {
    EXPECT_TRUE(s.add(c));
  }
  s.clear();
  for (auto c = lims::max() - 1; c > lims::min(); --c) {
    EXPECT_FALSE(s.contains(c));
    EXPECT_TRUE(s.add(c));
  }
}

TEST_F(SparseByteSetTest, remove) {
  for (auto c = lims::min(); c < lims::max(); ++c) {
    EXPECT_TRUE(s.add(c));
  }
  for (auto c = lims::min(); c < lims::max() / 2; ++c) {
    EXPECT_TRUE(s.remove(c));
    EXPECT_FALSE(s.contains(c));
  }

  // did not corrupt rest data
  for (auto c = lims::max() / 2; c < lims::max(); ++c) {
    EXPECT_TRUE(s.contains(c));
  }

  // check deleting last elements
  for (auto c = lims::max() - 1; c >= lims::max() / 2; --c) {
    EXPECT_TRUE(s.remove(c));
    EXPECT_FALSE(s.contains(c));
  }
}

TEST_F(SparseByteSetTest, remove_nop) {
  bool r = s.remove(12);
  EXPECT_FALSE(r);
}

TEST_F(SparseByteSetTest, size) {
  EXPECT_EQ(s.size(), 0);

  s.add(1);
  s.add(2);
  s.add(3);
  EXPECT_EQ(s.size(), 3);

  s.remove(1);
  EXPECT_EQ(s.size(), 2);
}
