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

#include <folly/Executor.h>

#include <stdexcept>

#include <glog/logging.h>

#include <folly/ExceptionString.h>
#include <folly/Portability.h>
#include <folly/lang/Exception.h>

namespace folly {

void Executor::invokeCatchingExnsLog(
    char const* const prefix, std::exception const* const ex) {
  auto const message = " threw unhandled ";
  if (ex) {
    LOG(ERROR) << prefix << message << exceptionStr(*ex);
  } else {
    auto ep = std::current_exception();
    LOG(ERROR) << prefix << message << exceptionStr(ep);
  }
}

void Executor::addWithPriority(Func, int8_t /* priority */) {
  throw std::runtime_error(
      "addWithPriority() is not implemented for this Executor");
}

bool Executor::keepAliveAcquire() noexcept {
  return false;
}

void Executor::keepAliveRelease() noexcept {
  LOG(FATAL) << __func__ << "() should not be called for folly::Executor types "
             << "which do not override keepAliveAcquire()";
}

#if defined(FOLLY_TLS)

extern constexpr bool const executor_blocking_list_enabled = true;
thread_local ExecutorBlockingList* executor_blocking_list = nullptr;

#else

extern constexpr bool const executor_blocking_list_enabled = false;
ExecutorBlockingList* executor_blocking_list = nullptr;

#endif

Optional<ExecutorBlockingContext> getExecutorBlockingContext() noexcept {
  return //
      !executor_blocking_list || !executor_blocking_list->forbid ? none : //
      make_optional(executor_blocking_list->curr);
}

ExecutorBlockingGuard::ExecutorBlockingGuard(PermitTag) noexcept
    : list_{false, executor_blocking_list, {}} {
  if (executor_blocking_list_enabled) {
    executor_blocking_list = &list_;
  }
}

ExecutorBlockingGuard::ExecutorBlockingGuard(
    TrackTag, StringPiece name) noexcept
    : list_{true, executor_blocking_list, {name}} {
  if (executor_blocking_list_enabled) {
    executor_blocking_list = &list_;
  }
}

ExecutorBlockingGuard::~ExecutorBlockingGuard() {
  if (executor_blocking_list_enabled) {
    if (executor_blocking_list != &list_) {
      terminate_with<std::logic_error>("dtor mismatch");
    }
    executor_blocking_list = list_.prev;
  }
}

} // namespace folly
