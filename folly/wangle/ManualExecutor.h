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

#pragma once
#include "folly/wangle/Executor.h"
#include <semaphore.h>
#include <memory>
#include <mutex>
#include <queue>

namespace folly { namespace wangle {

  class ManualExecutor : public Executor {
   public:
    ManualExecutor();

    void add(std::function<void()>&&) override;

    /// Do work. Returns the number of runnables that were executed (maybe 0).
    /// Non-blocking.
    size_t run();

    /// Wait for work to do.
    void wait();

    /// Wait for work to do, and do it.
    void makeProgress() {
      wait();
      run();
    }

   private:
    std::mutex lock_;
    std::queue<std::function<void()>> runnables_;
    sem_t sem_;
  };

}}
