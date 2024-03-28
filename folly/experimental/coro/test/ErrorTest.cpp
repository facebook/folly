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

#include <folly/Portability.h>

#include <folly/experimental/coro/Result.h>

#include <type_traits>

#include <folly/ExceptionWrapper.h>
#include <folly/Utility.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

class CoErrorTest : public testing::Test {};

TEST_F(CoErrorTest, constructible) {
  using namespace folly;
  using namespace folly::coro;

  EXPECT_TRUE((std::is_constructible_v<co_error, exception_wrapper>));
  EXPECT_TRUE((std::is_constructible_v<co_error, std::runtime_error>));
  EXPECT_TRUE((std::is_constructible_v<
               co_error,
               std::in_place_type_t<std::runtime_error>,
               std::string>));
  EXPECT_FALSE((std::is_constructible_v<co_error, int>));
}

#endif
