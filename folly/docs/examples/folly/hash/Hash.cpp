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

#include <folly/hash/Hash.h>
#include <folly/portability/GTest.h>

using namespace folly::hash;

TEST(hash, demo) {
  uint64_t a = 12345;
  uint64_t b = 67890;
  uint64_t ha = twang_mix64(a);
  uint64_t hb = twang_mix64(b);
  uint64_t h = hash_128_to_64(ha, hb);

  EXPECT_EQ(h, 3938132036471221536);

  uint64_t switched = hash_128_to_64(hb, ha); // switch order of ha, hb
  EXPECT_NE(h, switched); // hash_128_to_64 is order-dependent
}
