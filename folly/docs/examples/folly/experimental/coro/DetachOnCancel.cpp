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

#include <folly/experimental/coro/DetachOnCancel.h>
#include <folly/experimental/coro/GtestHelpers.h>
#include <folly/experimental/coro/Promise.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/Timeout.h>
#include <folly/portability/GTest.h>

using namespace std::literals::chrono_literals;

CO_TEST(CodeExamples, demoDetachOnCancel) {
  co_await folly::coro::co_awaitTry(folly::coro::timeout(
      folly::coro::co_invoke([]() -> folly::coro::Task<> {
        auto&& promiseFuture = folly::coro::makePromiseContract<void>();

        std::thread longTask(
            [promise = std::move(promiseFuture.first)]() mutable {
              // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
              std::this_thread::sleep_for(1s);
              LOG(INFO) << "Long running task finishes";
              promise.setValue();
            });

        try {
          co_await folly::coro::detachOnCancel(std::move(promiseFuture.second));
          LOG(INFO) << "DONE";
        } catch (folly::OperationCancelled&) {
          LOG(INFO) << "cancelled";
        }
        longTask.join();
      }),
      1s));
  co_return;
}
