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

#include "folly/AtomicHashArray.h"
#include "folly/Hash.h"
#include "folly/Conv.h"
#include <gtest/gtest.h>

using namespace std;
using namespace folly;

template<class KeyT, class ValueT>
pair<KeyT,ValueT> createEntry(int i) {
  return pair<KeyT,ValueT>(to<KeyT>(folly::hash::jenkins_rev_mix32(i) % 1000),
                           to<ValueT>(i + 3));
}

template<class KeyT, class ValueT>
void testMap() {
  typedef AtomicHashArray<KeyT, ValueT>  MyArr;
  auto arr = MyArr::create(150);
  map<KeyT, ValueT> ref;
  for (int i = 0; i < 100; ++i) {
    auto e = createEntry<KeyT, ValueT>(i);
    auto ret = arr->insert(e);
    EXPECT_EQ(!ref.count(e.first), ret.second);  // succeed iff not in ref
    ref.insert(e);
    EXPECT_EQ(ref.size(), arr->size());
    if (ret.first == arr->end()) {
      EXPECT_FALSE("AHA should not have run out of space.");
      continue;
    }
    EXPECT_EQ(e.first, ret.first->first);
    EXPECT_EQ(ref.find(e.first)->second, ret.first->second);
  }

  for (int i = 125; i > 0; i -= 10) {
    auto e = createEntry<KeyT, ValueT>(i);
    auto ret = arr->erase(e.first);
    auto refRet = ref.erase(e.first);
    EXPECT_EQ(ref.size(), arr->size());
    EXPECT_EQ(refRet, ret);
  }

  for (int i = 155; i > 0; i -= 10) {
    auto e = createEntry<KeyT, ValueT>(i);
    auto ret = arr->insert(e);
    auto refRet = ref.insert(e);
    EXPECT_EQ(ref.size(), arr->size());
    EXPECT_EQ(*refRet.first, *ret.first);
    EXPECT_EQ(refRet.second, ret.second);
  }

  for (const auto& e : ref) {
    auto ret = arr->find(e.first);
    if (ret == arr->end()) {
      EXPECT_FALSE("Key was not in AHA");
      continue;
    }
    EXPECT_EQ(e.first, ret->first);
    EXPECT_EQ(e.second, ret->second);
  }
}

TEST(Aha, InsertErase_i32_i32) {
  testMap<int32_t,int32_t>();
}
TEST(Aha, InsertErase_i64_i32) {
  testMap<int64_t,int32_t>();
}
TEST(Aha, InsertErase_i64_i64) {
  testMap<int64_t,int64_t>();
}
TEST(Aha, InsertErase_i32_i64) {
  testMap<int32_t,int64_t>();
}
TEST(Aha, InsertErase_i32_str) {
  testMap<int32_t,string>();
}
TEST(Aha, InsertErase_i64_str) {
  testMap<int64_t,string>();
}
