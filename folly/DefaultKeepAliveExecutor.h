/*
 * Copyright 2018-present Facebook, Inc.
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

#pragma once

#include <future>

#include <glog/logging.h>

#include <folly/Executor.h>
#include <folly/synchronization/Baton.h>

namespace folly {

/// An Executor accepts units of work with add(), which should be
/// threadsafe.
class DefaultKeepAliveExecutor : public virtual Executor {
 public:
  DefaultKeepAliveExecutor() : Executor() {}

  virtual ~DefaultKeepAliveExecutor() {
    DCHECK(!keepAlive_);
  }

 protected:
  void joinKeepAlive() {
    DCHECK(keepAlive_);
    keepAlive_.reset();
    keepAliveReleaseBaton_.wait();
  }

 private:
  bool keepAliveAcquire() override {
    auto keepAliveCounter =
        keepAliveCounter_.fetch_add(1, std::memory_order_relaxed);
    // We should never increment from 0
    DCHECK(keepAliveCounter > 0);
    return true;
  }

  void keepAliveRelease() override {
    auto keepAliveCounter = --keepAliveCounter_;
    DCHECK(keepAliveCounter >= 0);

    if (keepAliveCounter == 0) {
      keepAliveReleaseBaton_.post();
    }
  }

  std::atomic<ssize_t> keepAliveCounter_{1};
  Baton<> keepAliveReleaseBaton_;
  KeepAlive keepAlive_{makeKeepAlive()};
};

} // namespace folly
