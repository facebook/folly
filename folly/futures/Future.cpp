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

#include <folly/futures/Future.h>

#include <folly/Likely.h>
#include <folly/Singleton.h>
#include <folly/futures/HeapTimekeeper.h>
#include <folly/futures/ThreadWheelTimekeeper.h>
#include <folly/portability/GFlags.h>

FOLLY_GFLAGS_DEFINE_bool(
    folly_futures_use_thread_wheel_timekeeper,
    false,
    "Use ThreadWheelTimekeeper for the default Future timekeeper singleton");

namespace folly {
namespace futures {

SemiFuture<Unit> sleep(HighResDuration dur, Timekeeper* tk) {
  std::shared_ptr<Timekeeper> tks;
  if (FOLLY_LIKELY(!tk)) {
    tks = folly::detail::getTimekeeperSingleton();
    tk = tks.get();
  }

  if (FOLLY_UNLIKELY(!tk)) {
    return makeSemiFuture<Unit>(FutureNoTimekeeper());
  }

  return tk->after(dur);
}

Future<Unit> sleepUnsafe(HighResDuration dur, Timekeeper* tk) {
  return sleep(dur, tk).toUnsafeFuture();
}

namespace {
template <typename Ptr>
class FutureWaiter : public fibers::Baton::Waiter {
 public:
  FutureWaiter(Promise<Unit> promise, Ptr baton)
      : promise_(std::move(promise)), baton_(std::move(baton)) {
    baton_->setWaiter(*this);
  }

  void post() override {
    promise_.setValue();
    delete this;
  }

 private:
  Promise<Unit> promise_;
  Ptr baton_;
};
} // namespace

SemiFuture<Unit> wait(std::unique_ptr<fibers::Baton> baton) {
  Promise<Unit> promise;
  auto sf = promise.getSemiFuture();
  new FutureWaiter<std::unique_ptr<fibers::Baton>>(
      std::move(promise), std::move(baton));
  return sf;
}
SemiFuture<Unit> wait(std::shared_ptr<fibers::Baton> baton) {
  Promise<Unit> promise;
  auto sf = promise.getSemiFuture();
  new FutureWaiter<std::shared_ptr<fibers::Baton>>(
      std::move(promise), std::move(baton));
  return sf;
}

} // namespace futures

namespace detail {

namespace {
Singleton<Timekeeper, TimekeeperSingletonTag> gTimekeeperSingleton(
    []() -> Timekeeper* {
      if (FLAGS_folly_futures_use_thread_wheel_timekeeper) {
        return new ThreadWheelTimekeeper;
      } else {
        return new HeapTimekeeper;
      }
    });
} // namespace

std::shared_ptr<Timekeeper> getTimekeeperSingleton() {
  return gTimekeeperSingleton.try_get();
}

} // namespace detail

#if FOLLY_USE_EXTERN_FUTURE_UNIT
namespace futures {
namespace detail {
template class FutureBase<Unit>;
} // namespace detail
} // namespace futures

template class Future<Unit>;
template class SemiFuture<Unit>;
#endif

} // namespace folly
