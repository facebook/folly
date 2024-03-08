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

#include <folly/experimental/channels/detail/PointerVariant.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace channels {
namespace detail {

using namespace std::string_literals;

TEST(PointerVariantTest, Basic) {
  int64_t intVal1 = 100;
  int64_t intVal2 = 200;
  std::string strVal1 = "100"s;
  std::string strVal2 = "200"s;
  PointerVariant<int64_t, std::string> var(&intVal1);

  EXPECT_EQ(*var.get(folly::tag_t<int64_t>{}), 100);

  var.set(&intVal2);

  EXPECT_EQ(*var.get(folly::tag_t<int64_t>{}), 200);

  var.set(&strVal1);

  EXPECT_EQ(*var.get(folly::tag_t<std::string>{}), "100"s);

  var.set(&strVal2);

  EXPECT_EQ(*var.get(folly::tag_t<std::string>{}), "200"s);
}

TEST(PointerVariantTest, GetIncorrecttype) {
  int64_t intVal = 100;
  std::string strVal = "100"s;
  PointerVariant<int64_t, std::string> var(&intVal);

  EXPECT_THROW(var.get(folly::tag_t<std::string>{}), std::runtime_error);

  var.set(&strVal);

  EXPECT_THROW(var.get(folly::tag_t<int64_t>{}), std::runtime_error);
}
} // namespace detail
} // namespace channels
} // namespace folly
