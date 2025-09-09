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

#pragma once

#include <folly/coro/GtestHelpers.h>
#include <folly/result/coro.h>

namespace folly {

#define RESULT_CO_UNWRAP_BODY(body)                                  \
  {                                                                  \
    auto ret = body();                                               \
    if (!ret.has_value()) {                                          \
      if (ret.non_value().has_stopped()) {                           \
        FAIL() << "RESULT_CO_TEST got cancellation";                 \
      } else {                                                       \
        FAIL() << std::move(ret).non_value().to_exception_wrapper(); \
      }                                                              \
    }                                                                \
  }

/*
Analog of GTest `TEST()` macro for writing `result` coroutine tests.

For assertions, use either standard `EXPECT_*` macros, or `CO_ASSERT_*` from
`folly/coro/GtestHelpers.h`.
*/
#define RESULT_CO_TEST(test_case_name, test_name) \
  CO_TEST_(                                       \
      test_case_name,                             \
      test_name,                                  \
      ::testing::Test,                            \
      ::testing::internal::GetTestTypeId(),       \
      ::folly::result<>,                          \
      RESULT_CO_UNWRAP_BODY)

/*
Analog of GTest `TEST_F()` macro for writing `result` coroutine tests.

For assertions, use either standard `EXPECT_*` macros, or `CO_ASSERT_*` from
`folly/coro/GtestHelpers.h`.
*/
#define RESULT_CO_TEST_F(test_fixture, test_name)     \
  CO_TEST_(                                           \
      test_fixture,                                   \
      test_name,                                      \
      test_fixture,                                   \
      ::testing::internal::GetTypeId<test_fixture>(), \
      ::folly::result<>,                              \
      RESULT_CO_UNWRAP_BODY)

} // namespace folly
