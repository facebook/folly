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

#include <folly/hash/FnvHash.h>

#include <folly/portability/GTest.h>

using namespace folly::hash;

TEST(FnvHash, Fnv32_BROKEN) {
  const char* s1 = "hello, world!";
  const uint32_t s1_res = 3605494790UL;
  EXPECT_EQ(fnv32_BROKEN(s1), s1_res);
  EXPECT_EQ(fnv32_BROKEN(s1), fnv32_buf_BROKEN(s1, strlen(s1)));

  const char* s2 = "monkeys! m0nk3yz! ev3ry \\/\\/here~~~~";
  const uint32_t s2_res = 1270448334UL;
  EXPECT_EQ(fnv32_BROKEN(s2), s2_res);
  EXPECT_EQ(fnv32_BROKEN(s2), fnv32_buf_BROKEN(s2, strlen(s2)));

  const char* s3 = "";
  const uint32_t s3_res = 2166136261UL;
  EXPECT_EQ(fnv32_BROKEN(s3), s3_res);
  EXPECT_EQ(fnv32_BROKEN(s3), fnv32_buf_BROKEN(s3, strlen(s3)));

  const uint8_t s4_data[] = {0xFF, 0xFF, 0xFF, 0x00};
  const char* s4 = reinterpret_cast<const char*>(s4_data);
  const uint32_t s4_res = 2420936562UL;
  EXPECT_EQ(fnv32_BROKEN(s4), s4_res);
  EXPECT_EQ(fnv32_BROKEN(s4), fnv32_buf_BROKEN(s4, strlen(s4)));
}

TEST(FnvHash, Fnv64_BROKEN) {
  const char* s1 = "hello, world!";
  const uint64_t s1_res = 13991426986746681734ULL;
  EXPECT_EQ(fnv64_BROKEN(s1), s1_res);
  EXPECT_EQ(fnv64_BROKEN(s1), fnv64_buf_BROKEN(s1, strlen(s1)));

  const char* s2 = "monkeys! m0nk3yz! ev3ry \\/\\/here~~~~";
  const uint64_t s2_res = 6091394665637302478ULL;
  EXPECT_EQ(fnv64_BROKEN(s2), s2_res);
  EXPECT_EQ(fnv64_BROKEN(s2), fnv64_buf_BROKEN(s2, strlen(s2)));

  const char* s3 = "";
  const uint64_t s3_res = 14695981039346656037ULL;
  EXPECT_EQ(fnv64_BROKEN(s3), s3_res);
  EXPECT_EQ(fnv64_BROKEN(s3), fnv64_buf_BROKEN(s3, strlen(s3)));

  const uint8_t s4_data[] = {0xFF, 0xFF, 0xFF, 0x00};
  const char* s4 = reinterpret_cast<const char*>(s4_data);
  const uint64_t s4_res = 2787597222566293202ULL;
  EXPECT_EQ(fnv64_BROKEN(s4), s4_res);
  EXPECT_EQ(fnv64_BROKEN(s4), fnv64_buf_BROKEN(s4, strlen(s4)));

  // note: Use fnv64_buf to make a single hash value from multiple
  // fields/datatypes.
  const char* t4_a = "E Pluribus";
  int64_t t4_b = 0xF1E2D3C4B5A69788;
  int32_t t4_c = 0xAB12CD34;
  const char* t4_d = "Unum";
  uint64_t t4_res = 15571330457339273965ULL;
  uint64_t t4_hash1 = fnv64_buf_BROKEN(t4_a, strlen(t4_a));
  uint64_t t4_hash2 = fnv64_buf_BROKEN(
      reinterpret_cast<void*>(&t4_b), sizeof(int64_t), t4_hash1);
  uint64_t t4_hash3 = fnv64_buf_BROKEN(
      reinterpret_cast<void*>(&t4_c), sizeof(int32_t), t4_hash2);
  uint64_t t4_hash4 = fnv64_buf_BROKEN(t4_d, strlen(t4_d), t4_hash3);
  EXPECT_EQ(t4_hash4, t4_res);
  // note: These are probabalistic, not determinate, but c'mon.
  // These hash values should be different, or something's not
  // working.
  EXPECT_NE(t4_hash1, t4_hash4);
  EXPECT_NE(t4_hash2, t4_hash4);
  EXPECT_NE(t4_hash3, t4_hash4);
}

TEST(FnvHash, Fnv32_FIXED) {
  const char* s1 = "hello, world!";
  const uint32_t s1_res = 3605494790U;
  EXPECT_EQ(fnv32_FIXED(s1), s1_res);
  EXPECT_EQ(fnv32_FIXED(s1), fnv32_buf_FIXED(s1, strlen(s1)));

  const char* s2 = "monkeys! m0nk3yz! ev3ry \\/\\/here~~~~";
  const uint32_t s2_res = 1270448334U;
  EXPECT_EQ(fnv32_FIXED(s2), s2_res);
  EXPECT_EQ(fnv32_FIXED(s2), fnv32_buf_FIXED(s2, strlen(s2)));

  const char* s3 = "";
  const uint32_t s3_res = 2166136261U;
  EXPECT_EQ(fnv32_FIXED(s3), s3_res);
  EXPECT_EQ(fnv32_FIXED(s3), fnv32_buf_FIXED(s3, strlen(s3)));

  const uint8_t s4_data[] = {0xFF, 0xFF, 0xFF, 0x00};
  const char* s4 = reinterpret_cast<const char*>(s4_data);
  const uint32_t s4_res = 2978929266U;
  EXPECT_EQ(fnv32_FIXED(s4), s4_res);
  EXPECT_EQ(fnv32_FIXED(s4), fnv32_buf_FIXED(s4, strlen(s4)));
}

TEST(FnvHash, Fnv64_FIXED) {
  const char* s1 = "hello, world!";
  const uint64_t s1_res = 13991426986746681734ULL;
  EXPECT_EQ(fnv64_FIXED(s1), s1_res);
  EXPECT_EQ(fnv64_FIXED(s1), fnv64_buf_FIXED(s1, strlen(s1)));

  const char* s2 = "monkeys! m0nk3yz! ev3ry \\/\\/here~~~~";
  const uint64_t s2_res = 6091394665637302478ULL;
  EXPECT_EQ(fnv64_FIXED(s2), s2_res);
  EXPECT_EQ(fnv64_FIXED(s2), fnv64_buf_FIXED(s2, strlen(s2)));

  const char* s3 = "";
  const uint64_t s3_res = 14695981039346656037ULL;
  EXPECT_EQ(fnv64_FIXED(s3), s3_res);
  EXPECT_EQ(fnv64_FIXED(s3), fnv64_buf_FIXED(s3, strlen(s3)));

  const uint8_t s4_data[] = {0xFF, 0xFF, 0xFF, 0x00};
  const char* s4 = reinterpret_cast<const char*>(s4_data);
  const uint64_t s4_res = 15475554797547429842ULL;
  EXPECT_EQ(fnv64_FIXED(s4), s4_res);
  EXPECT_EQ(fnv64_FIXED(s4), fnv64_buf_FIXED(s4, strlen(s4)));

  // note: Use fnv64_buf to make a single hash value from multiple
  // fields/datatypes.
  const char* t4_a = "E Pluribus";
  int64_t t4_b = 0xF1E2D3C4B5A69788;
  int32_t t4_c = 0xAB12CD34;
  const char* t4_d = "Unum";
  uint64_t t4_res = 14526396152295369453ULL;
  uint64_t t4_hash1 = fnv64_buf_FIXED(t4_a, strlen(t4_a));
  uint64_t t4_hash2 = fnv64_buf_FIXED(
      reinterpret_cast<void*>(&t4_b), sizeof(int64_t), t4_hash1);
  uint64_t t4_hash3 = fnv64_buf_FIXED(
      reinterpret_cast<void*>(&t4_c), sizeof(int32_t), t4_hash2);
  uint64_t t4_hash4 = fnv64_buf_FIXED(t4_d, strlen(t4_d), t4_hash3);
  EXPECT_EQ(t4_hash4, t4_res);
  // note: These are probabalistic, not determinate, but c'mon.
  // These hash values should be different, or something's not
  // working.
  EXPECT_NE(t4_hash1, t4_hash4);
  EXPECT_NE(t4_hash2, t4_hash4);
  EXPECT_NE(t4_hash3, t4_hash4);
}
