/*
 * Copyright 2014-present Facebook, Inc.
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

#include <folly/futures/Future.h>
#include <folly/Likely.h>
#include <folly/SingletonThreadLocal.h>
#include <folly/futures/ThreadWheelTimekeeper.h>

namespace folly {

// Instantiate the most common Future types to save compile time
template class SemiFuture<Unit>;
template class SemiFuture<bool>;
template class SemiFuture<int>;
template class SemiFuture<int64_t>;
template class SemiFuture<std::string>;
template class SemiFuture<double>;
template class Future<Unit>;
template class Future<bool>;
template class Future<int>;
template class Future<int64_t>;
template class Future<std::string>;
template class Future<double>;
} // namespace folly

namespace folly {
namespace futures {

Future<Unit> sleep(Duration dur, Timekeeper* tk) {
  std::shared_ptr<Timekeeper> tks;
  if (LIKELY(!tk)) {
    tks = folly::detail::getTimekeeperSingleton();
    tk = tks.get();
  }

  if (UNLIKELY(!tk)) {
    return makeFuture<Unit>(NoTimekeeper());
  }

  return tk->after(dur);
}

namespace detail {

struct TimedDrivableExecutorWrapperTag {};
struct TimedDrivableExecutorWrapperMaker {
  std::pair<folly::Executor::KeepAlive, TimedDrivableExecutor*> operator()() {
    std::unique_ptr<futures::detail::TimedDrivableExecutorWrapper> devb{
        new futures::detail::TimedDrivableExecutorWrapper{}};
    std::pair<folly::Executor::KeepAlive, TimedDrivableExecutor*> ret{
        devb->getKeepAliveToken(),
        static_cast<TimedDrivableExecutor*>(devb.get())};
    devb.release();
    return ret;
  }
};

std::pair<folly::Executor::KeepAlive, TimedDrivableExecutor*>
TimedDrivableExecutorWrapper::get() {
  // Thread-local is a pair of a KeepAlive that owns the executor safely
  // relative to later created keepalives, and a pre-cast pointer to avoid
  // recasting later (which would have to be dynamic from Executor*).
  auto& p = folly::SingletonThreadLocal<
      std::pair<folly::Executor::KeepAlive, TimedDrivableExecutor*>,
      TimedDrivableExecutorWrapperTag,
      TimedDrivableExecutorWrapperMaker>::get();
  // Reconstruct pair from a new keepalive token and the pointer because
  // the keepalive is not copyable
  return {p.second->getKeepAliveToken(), p.second};
}

void TimedDrivableExecutorWrapper::keepAliveAcquire() {
  keepAliveCount_.fetch_add(1, std::memory_order_relaxed);
}

void TimedDrivableExecutorWrapper::keepAliveRelease() {
  if (keepAliveCount_.fetch_sub(1, std::memory_order_release) == 1) {
    std::atomic_thread_fence(std::memory_order_acquire);
    delete this;
  }
}

folly::Executor::KeepAlive TimedDrivableExecutorWrapper::getKeepAliveToken() {
  keepAliveAcquire();
  return makeKeepAlive();
}

} // namespace detail

} // namespace futures
} // namespace folly
