/*
 * Copyright 2015 Facebook, Inc.
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

#include <gtest/gtest.h>

#include <folly/futures/Future.h>

using namespace folly;

TEST(Ensure, basic) {
  size_t count = 0;
  auto cob = [&]{ count++; };
  auto f = makeFuture(42)
    .ensure(cob)
    .then([](int) { throw std::runtime_error("ensure"); })
    .ensure(cob);

  EXPECT_THROW(f.get(), std::runtime_error);
  EXPECT_EQ(2, count);
}
