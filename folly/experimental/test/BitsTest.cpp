/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/experimental/Bits.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;

TEST(Bits, Simple) {
  EXPECT_EQ(0, Bits<uint8_t>::blockCount(0));
  EXPECT_EQ(1, Bits<uint8_t>::blockCount(1));
  EXPECT_EQ(1, Bits<uint8_t>::blockCount(8));
  EXPECT_EQ(2, Bits<uint8_t>::blockCount(9));
  EXPECT_EQ(256, Bits<uint8_t>::blockCount(2048));
  EXPECT_EQ(257, Bits<uint8_t>::blockCount(2049));

  EXPECT_EQ(4, Bits<uint8_t>::blockIndex(39));
  EXPECT_EQ(7, Bits<uint8_t>::bitOffset(39));
  EXPECT_EQ(5, Bits<uint8_t>::blockIndex(40));
  EXPECT_EQ(0, Bits<uint8_t>::bitOffset(40));

  uint8_t buf[256];
  memset(buf, 0, 256);

  Bits<uint8_t>::set(buf, 36);
  Bits<uint8_t>::set(buf, 39);
  EXPECT_EQ((1 << 7) | (1 << 4), buf[4]);
  EXPECT_EQ(0, buf[5]);
  Bits<uint8_t>::clear(buf, 39);
  EXPECT_EQ(1 << 4, buf[4]);
  EXPECT_EQ(0, buf[5]);
  Bits<uint8_t>::set(buf, 40);
  EXPECT_EQ(1 << 4, buf[4]);
  EXPECT_EQ(1, buf[5]);

  EXPECT_EQ(2, Bits<uint8_t>::count(buf, buf + 256));
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}

