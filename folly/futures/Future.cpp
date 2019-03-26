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

SemiFuture<Unit> sleep(Duration dur, Timekeeper* tk) {
  std::shared_ptr<Timekeeper> tks;
  if (LIKELY(!tk)) {
    tks = folly::detail::getTimekeeperSingleton();
    tk = tks.get();
  }

  if (UNLIKELY(!tk)) {
    return makeSemiFuture<Unit>(FutureNoTimekeeper());
  }

  return tk->after(dur);
}

Future<Unit> sleepUnsafe(Duration dur, Timekeeper* tk) {
  return sleep(dur, tk).toUnsafeFuture();
}

#if FOLLY_FUTURE_USING_FIBER

namespace {
class FutureWaiter : public fibers::Baton::Waiter {
 public:
  FutureWaiter(Promise<Unit> promise, std::unique_ptr<fibers::Baton> baton)
      : promise_(std::move(promise)), baton_(std::move(baton)) {
    baton_->setWaiter(*this);
  }

  void post() override {
    promise_.setValue();
    delete this;
  }

 private:
  Promise<Unit> promise_;
  std::unique_ptr<fibers::Baton> baton_;
};
} // namespace

SemiFuture<Unit> wait(std::unique_ptr<fibers::Baton> baton) {
  Promise<Unit> promise;
  auto sf = promise.getSemiFuture();
  new FutureWaiter(std::move(promise), std::move(baton));
  return sf;
}

#endif

} // namespace futures
} // namespace folly
