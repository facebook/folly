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

#include <folly/lang/Extern.h>

#include <folly/portability/GTest.h>

namespace folly::extern_test {
extern int foobar(int);
}

static int hasdef(int a) {
  return -a;
}

FOLLY_CREATE_EXTERN_ACCESSOR(access_foobar_v, folly::extern_test::foobar);
FOLLY_CREATE_EXTERN_ACCESSOR(access_hasdef_v, hasdef);

class ExternTest : public testing::Test {};

TEST_F(ExternTest, example) {
  EXPECT_EQ(nullptr, access_foobar_v<false>);
  EXPECT_EQ(nullptr, access_hasdef_v<false>);
  EXPECT_EQ(&hasdef, access_hasdef_v<true>);
  EXPECT_EQ(-17, access_hasdef_v<true>(17));
}
