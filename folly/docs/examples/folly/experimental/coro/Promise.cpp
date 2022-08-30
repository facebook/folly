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

#include <chrono>
#include <thread>

#include <folly/experimental/coro/GtestHelpers.h>
#include <folly/experimental/coro/Promise.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>

using namespace std::literals::chrono_literals;

auto sleepAndNotify(std::function<void(int)>&& cob) {
  return std::thread(
      [cob = std::forward<std::function<void(int)>>(cob)]() mutable {
        // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
        std::this_thread::sleep_for(1s);
        cob(1);
      });
}

CO_TEST(CodeExamples, promiseFuturePoc) {
  auto&& pf = folly::coro::makePromiseContract<int>();

  auto job = sleepAndNotify(
      [promise = std::make_shared<folly::coro::Promise<int>>(
           std::move(pf.first))](int i) mutable { promise->setValue(i); });

  CO_ASSERT_EQ(co_await std::move(pf.second), 1);

  job.join();
}
