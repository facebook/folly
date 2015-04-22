/*
 * Copyright 2015 Facebook, Inc.
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
#include "ThreadWheelTimekeeper.h"

#include <folly/Singleton.h>
#include <folly/futures/Future.h>
#include <future>

namespace folly { namespace detail {

namespace {
  Singleton<ThreadWheelTimekeeper> timekeeperSingleton_;

  // Our Callback object for HHWheelTimer
  struct WTCallback : public folly::HHWheelTimer::Callback {
    // Only allow creation by this factory, to ensure heap allocation.
    static WTCallback* create() {
      // optimization opportunity: memory pool
      return new WTCallback();
    }

    Future<void> getFuture() {
      return promise_.getFuture();
    }

   protected:
    Promise<void> promise_;

    explicit WTCallback() {
      promise_.setInterruptHandler(
        std::bind(&WTCallback::interruptHandler, this));
    }

    void timeoutExpired() noexcept override {
      promise_.setValue();
      delete this;
    }

    void interruptHandler() {
      cancelTimeout();
      delete this;
    }
  };

} // namespace


ThreadWheelTimekeeper::ThreadWheelTimekeeper() :
  thread_([this]{ eventBase_.loopForever(); }),
  wheelTimer_(new HHWheelTimer(&eventBase_, std::chrono::milliseconds(1)))
{
  eventBase_.waitUntilRunning();
  eventBase_.runInEventBaseThread([this]{
    // 15 characters max
    eventBase_.setName("FutureTimekeepr");
  });
}

ThreadWheelTimekeeper::~ThreadWheelTimekeeper() {
  eventBase_.runInEventBaseThreadAndWait([this]{
    wheelTimer_->cancelAll();
  });
  eventBase_.terminateLoopSoon();
  thread_.join();
}

Future<void> ThreadWheelTimekeeper::after(Duration dur) {
  auto cob = WTCallback::create();
  auto f = cob->getFuture();
  eventBase_.runInEventBaseThread([=]{
    wheelTimer_->scheduleTimeout(cob, dur);
  });
  return f;
}

Timekeeper* getTimekeeperSingleton() {
  return timekeeperSingleton_.get();
}

}} // folly::detail
