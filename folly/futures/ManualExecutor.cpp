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

#include <folly/futures/ManualExecutor.h>

#include <string.h>
#include <string>
#include <tuple>

#include <stdexcept>

namespace folly {

ManualExecutor::ManualExecutor() {
  if (sem_init(&sem_, 0, 0) == -1) {
    throw std::runtime_error(std::string("sem_init: ") + strerror(errno));
  }
}

void ManualExecutor::add(Func callback) {
  std::lock_guard<std::mutex> lock(lock_);
  funcs_.push(std::move(callback));
  sem_post(&sem_);
}

size_t ManualExecutor::run() {
  size_t count;
  size_t n;
  Func func;

  {
    std::lock_guard<std::mutex> lock(lock_);

    while (!scheduledFuncs_.empty()) {
      auto& sf = scheduledFuncs_.top();
      if (sf.time > now_)
        break;
      funcs_.push(sf.func);
      scheduledFuncs_.pop();
    }

    n = funcs_.size();
  }

  for (count = 0; count < n; count++) {
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (funcs_.empty()) {
        break;
      }

      // Balance the semaphore so it doesn't grow without bound
      // if nobody is calling wait().
      // This may fail (with EAGAIN), that's fine.
      sem_trywait(&sem_);

      func = std::move(funcs_.front());
      funcs_.pop();
    }
    func();
  }

  return count;
}

void ManualExecutor::wait() {
  while (true) {
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (!funcs_.empty())
        break;
    }

    auto ret = sem_wait(&sem_);
    if (ret == 0) {
      break;
    }
    if (errno != EINVAL) {
      throw std::runtime_error(std::string("sem_wait: ") + strerror(errno));
    }
  }
}

void ManualExecutor::advanceTo(TimePoint const& t) {
  if (t > now_) {
    now_ = t;
  }
  run();
}

} // folly
