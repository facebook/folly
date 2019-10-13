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

#include <type_traits>

#include <chrono>
using namespace std::literals;

#include <folly/experimental/pushmi/sender/flow_single_sender.h>
#include <folly/experimental/pushmi/o/empty.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/for_each.h>
#include <folly/experimental/pushmi/o/from.h>
#include <folly/experimental/pushmi/o/just.h>
#include <folly/experimental/pushmi/o/transform.h>

#include <folly/experimental/pushmi/executor/trampoline.h>

using namespace folly::pushmi::aliases;

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace testing;

auto l_x2 = [](int x) {
  return x * x;
};

struct mo_x2 {
  mo_x2() = default;
  mo_x2(mo_x2&&) = default;
  mo_x2& operator=(mo_x2&&) = default;
  mo_x2(const mo_x2&) = delete;
  mo_x2& operator=(const mo_x2&) = delete;
  int operator()(int x) const {
    return x * x;
  }
};

TEST(Lambda, Transform) {
  auto v = op::just(42);

  v | op::transform(l_x2) |
      op::submit();
}

TEST(MoveOnly, Transform) {
  auto v = op::just(42);

  v | op::transform(mo_x2{}) |
      op::submit();
}

TEST(ManyLambda, Transform) {
  std::array<int, 1> a{{42}};
  auto v = op::from(a);

  v | op::transform(l_x2) |
      op::submit();
}

TEST(ManyMoveOnly, Transform) {
  std::array<int, 1> a{{42}};
  auto v = op::from(a);

  v | op::transform(mo_x2{}) |
      op::submit();
}

TEST(FlowManyLambda, Transform) {
  std::array<int, 1> a{{42}};
  auto v = op::flow_from(a);

  v | op::transform(l_x2) |
      op::for_each();
}

TEST(FlowManyMoveOnly, Transform) {
  std::array<int, 1> a{{42}};
  auto v = op::flow_from(a);

  v | op::transform(mo_x2{}) |
      op::for_each();
}
