/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/lang/Cast.h>

#include <folly/Utility.h>
#include <folly/portability/GTest.h>

using folly::as_const;
using folly::down_cast;

class CastTest : public testing::Test {};

template <typename T>
static T& stop(T&& t) {
  return t;
}
template <typename T>
static void stop(T& t) = delete;

TEST_F(CastTest, down_cast) {
  struct base {
    virtual ~base() {}
  };
  struct derived : public base {};

  derived obj;
  base& b = obj;

  EXPECT_TRUE(&obj == down_cast<derived>(&b));
  EXPECT_TRUE(&obj == down_cast<derived>(&as_const(b)));
  EXPECT_TRUE(&obj == &stop(down_cast<derived>(std::move(b))));
  EXPECT_TRUE(&obj == &stop(down_cast<derived>(std::move(as_const(b)))));
}
