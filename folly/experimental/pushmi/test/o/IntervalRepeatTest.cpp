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

#include <folly/experimental/pushmi/o/defer.h>
#include <folly/experimental/pushmi/o/filter.h>
#include <folly/experimental/pushmi/o/interval_repeat.h>
#include <folly/experimental/pushmi/o/just.h>
#include <folly/experimental/pushmi/o/tap.h>

#include <folly/experimental/pushmi/executor/new_thread.h>
#include <folly/experimental/pushmi/executor/time_source.h>

using namespace folly::pushmi::aliases;

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace testing;

TEST(Now, IntervalRepeat) {
  auto nt = mi::new_thread();

  auto time = mi::time_source<>{};
  auto now = mi::systemNowF{};
  auto strands = time.make(now, nt);

  auto start = now();
  auto finish = now() + 1s;

  std::vector<int> actual;

  op::get<decltype(finish)>(
      op::defer([&]() { return op::just(now()); }) |
      op::filter([&](auto tick) { return tick < finish; }) |
      op::tap([&](auto tick) {
        // convert to ms so that the results are integers
        // divide by 100 to discard the jitter so that the test is reliable.
        actual.push_back(
            std::chrono::duration_cast<std::chrono::milliseconds>(tick - start)
                .count() /
            100);
        std::cout << actual.back() << std::endl;
      }) |
      op::interval_repeat(strands, start, 100ms) |
      op::tap(
          mi::on_done([&]() {
            std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(
                             now() - start)
                             .count()
                      << std::endl;
          })));

  EXPECT_THAT(actual.size(), Eq(10));
  EXPECT_THAT(actual, Eq(decltype(actual){{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}}));

  time.join();
}
