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
#include <folly/experimental/pushmi/o/from.h>
#include <folly/experimental/pushmi/o/just.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/tap.h>
#include <folly/experimental/pushmi/o/transform.h>

using namespace folly::pushmi::aliases;

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace testing;

TEST(EmptySingleSender, TapAndSubmit) {
  auto e = op::empty();
  using E = decltype(e);

  EXPECT_THAT(
    (v::SenderTo<E, v::any_receiver<>> && v::SingleSender<E>),
    Eq(true))
      << "expected empty to return a single sender that can take an any_receiver<>";

  EXPECT_THAT(
      (v::SenderTo<
          E,
          v::any_receiver<std::exception_ptr, int>> &&
          v::SingleSender<E>),
      Eq(true))
      << "expected empty to return a single sender that can take an any_receiver<int>";

  int signals = 0;
  e |
      op::tap(
          [&]() { signals += 100; },
          [&](auto) noexcept { signals += 1000; },
          [&]() { signals += 10; }) |
      op::submit(
          [&]() { signals += 100; },
          [&](auto) noexcept { signals += 1000; },
          [&]() { signals += 10; });

  EXPECT_THAT(signals, Eq(20))
      << "expected the done signal to be recorded twice";

  EXPECT_THROW(v::future_from(e).get(), std::future_error)
      << "expected future_error when future_from is applied";

  EXPECT_THAT(
      (std::is_same<std::future<void>, decltype(v::future_from(e))>::value),
      Eq(true))
      << "expected future_from(e) to return std::future<void>";
}

TEST(JustIntSingleSender, TransformAndSubmit) {
  auto j = op::just(20);
  using J = decltype(j);

  EXPECT_THAT(
      (v::SenderTo<J,v::any_receiver<std::exception_ptr, int>> &&
        v::SingleSender<J>),
      Eq(true))
      << "expected empty to return a single sender that can take an any_receiver<int>";

  int signals = 0;
  int value = 0;
  std::move(j) |
      op::transform(
          [&](int v) {
            signals += 10000;
            return v + 1;
          },
          [&](auto v) {
            std::terminate();
            return v;
          }) |
      op::transform([&](int v) {
        signals += 10000;
        return v * 2;
      }) |
      op::submit(
          [&](auto v) {
            value = v;
            signals += 100;
          },
          [&](auto) noexcept { signals += 1000; },
          [&]() { signals += 10; });

  EXPECT_THAT(signals, Eq(20110))
      << "expected that the transform signal is recorded twice and that the value and done signals once each";

  EXPECT_THAT(value, Eq(42)) << "expected a different result";

  auto twenty = v::future_from<int>(j).get();

  EXPECT_THAT(twenty, Eq(20))
      << "expected a different result from future_from(e).get()";

  EXPECT_THAT(
      (std::is_same<std::future<int>, decltype(v::future_from<int>(j))>::value),
      Eq(true))
      << "expected future_from(e) to return std::future<int>";
}

TEST(FromIntManySender, TransformAndSubmit) {
  std::array<int, 3> arr{{0, 9, 99}};
  auto m = op::from(arr);
  using M = decltype(m);

  EXPECT_THAT(
      (v::SenderTo<M, v::any_receiver<std::exception_ptr, int>>),
      Eq(true))
      << "expected empty to return a many sender that can take an any_receiver<int>";

  int signals = 0;
  int value = 0;
  m |
      op::transform(
          [&](int v) {
            signals += 10000;
            return v + 1;
          },
          [&](auto v) {
            std::terminate();
            return v;
          }) |
      op::transform([&](int v) {
        signals += 10000;
        return v * 2;
      }) |
      op::submit(
          [&](auto v) {
            value += v;
            signals += 100;
          },
          [&](auto) noexcept { signals += 1000; },
          [&]() { signals += 10; });

  EXPECT_THAT(signals, Eq(60310))
      << "expected that the transform signal is recorded six times and that the value signal three times and done signal once";

  EXPECT_THAT(value, Eq(222)) << "expected a different result";
}
