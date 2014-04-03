/*
 * Copyright 2014 Facebook, Inc.
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

#include "ManualExecutor.h"

#include <string.h>
#include <string>

#include <stdexcept>

namespace folly { namespace wangle {

ManualExecutor::ManualExecutor() {
  if (sem_init(&sem_, 0, 0) == -1) {
    throw std::runtime_error(std::string("sem_init: ") + strerror(errno));
  }
}

void ManualExecutor::add(std::function<void()>&& callback) {
  std::lock_guard<std::mutex> lock(lock_);
  runnables_.push(callback);
  sem_post(&sem_);
}

size_t ManualExecutor::run() {
  size_t count;
  size_t n;
  std::function<void()> runnable;

  {
    std::lock_guard<std::mutex> lock(lock_);
    n = runnables_.size();
  }

  for (count = 0; count < n; count++) {
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (runnables_.empty()) {
        break;
      }

      // Balance the semaphore so it doesn't grow without bound
      // if nobody is calling wait().
      // This may fail (with EAGAIN), that's fine.
      sem_trywait(&sem_);

      runnable = std::move(runnables_.front());
      runnables_.pop();
    }
    runnable();
  }

  return count;
}

void ManualExecutor::wait() {
  while (true) {
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (!runnables_.empty())
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

}} // namespace
