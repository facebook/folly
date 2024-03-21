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

#include <folly/memory/Malloc.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Malloc.h>
#include <folly/test/TestUtils.h>

namespace folly {

TEST(MallocTest, naiveGoodMallocSizeMatchesJEMalloc) {
  if (!usingJEMalloc()) {
    return;
  }

  EXPECT_EQ(naiveGoodMallocSize(0), 0);

  size_t s = 1;
  while (s < 1025) {
    EXPECT_EQ(naiveGoodMallocSize(s), nallocx(s, 0)) << "s == " << s;
    s++;
  }

  while (s < (1ULL << 60)) {
    EXPECT_EQ(naiveGoodMallocSize(s - 1), nallocx(s - 1, 0)) << "s == " << s;
    EXPECT_EQ(naiveGoodMallocSize(s), nallocx(s, 0)) << "s == " << s;
    EXPECT_EQ(naiveGoodMallocSize(s + 1), nallocx(s + 1, 0)) << "s == " << s;
    s = nallocx(s + 1, 0);
  }
}

template <typename T, size_t kGoodSize = naiveGoodMallocSize(sizeof(T))>
struct GoodSizeConstexprChecker {
  static constexpr size_t value() { return kGoodSize; }
};

// This is a compilation check to make sure large and small calls can be used
// in constexpr contexts.
TEST(MallocTest, staticConstexprCheck) {
  static constexpr int kSmall = 17;
  static constexpr int kLarge = 217;

  struct SmallStruct {
    char array[kSmall];
  };

  struct LargeStruct {
    char array[kLarge];
  };

  EXPECT_EQ(
      GoodSizeConstexprChecker<SmallStruct>::value(),
      naiveGoodMallocSize(kSmall));
  EXPECT_EQ(
      GoodSizeConstexprChecker<LargeStruct>::value(),
      naiveGoodMallocSize(kLarge));
}

TEST(MallocTest, getJEMallocMallctlArenasAll) {
  SKIP_IF(!usingJEMalloc());

  EXPECT_EQ(4096, getJEMallocMallctlArenasAll());
}

} // namespace folly
