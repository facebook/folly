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

#include <folly/synchronization/HazptrDomain.h>

#include <cstddef>
#include <queue>
#include <type_traits>

#include <glog/logging.h>
#include <folly/ExceptionString.h>
#include <folly/Function.h>
#include <folly/ScopeGuard.h>
#include <folly/lang/Exception.h>

namespace folly::detail {

void hazptr_inline_executor_add(folly::Function<void()> func) {
  using Queue = std::queue<folly::Function<void()>>;
  thread_local Queue* current = nullptr;

  if (current != nullptr) {
    current->push(std::move(func));
    return;
  }

  Queue queue;
  current = &queue;
  auto cleanup = makeGuard([&] { current = nullptr; });
  auto logException = []() noexcept {
    LOG(ERROR) << "HazptrDomain threw unhandled "
               << exceptionStr(current_exception());
  };

  catch_exception(std::exchange(func, {}), logException);

  while (!queue.empty()) {
    catch_exception(std::ref(queue.front()), logException);
    queue.pop();
  }
}

} // namespace folly::detail
