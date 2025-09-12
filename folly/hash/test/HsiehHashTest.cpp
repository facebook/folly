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

#include <folly/hash/HsiehHash.h>

#include <folly/portability/GTest.h>

using namespace folly::hash;

TEST(HsiehHash, Hsieh32) {
  const char* s1 = "hello, world!";
  const uint32_t s1_res = 2918802987ul;
  EXPECT_EQ(hsieh_hash32(s1), s1_res);
  EXPECT_EQ(hsieh_hash32(s1), hsieh_hash32_buf(s1, strlen(s1)));

  const char* s2 = "monkeys! m0nk3yz! ev3ry \\/\\/here~~~~";
  const uint32_t s2_res = 47373213ul;
  EXPECT_EQ(hsieh_hash32(s2), s2_res);
  EXPECT_EQ(hsieh_hash32(s2), hsieh_hash32_buf(s2, strlen(s2)));

  const char* s3 = "";
  const uint32_t s3_res = 0;
  EXPECT_EQ(hsieh_hash32(s3), s3_res);
  EXPECT_EQ(hsieh_hash32(s3), hsieh_hash32_buf(s3, strlen(s3)));
}
