/*
 * Copyright 2017 Facebook, Inc.
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

#include "TestExecutor.h"

using namespace std;

namespace folly {

TestExecutor::TestExecutor() {
  const auto kWorkers = std::max(1U, thread::hardware_concurrency());
  for (auto idx = 0U; idx < kWorkers; ++idx) {
    workers_.emplace_back([this] {
      while (true) {
        Func work;
        {
          unique_lock<mutex> lk(m_);
          cv_.wait(lk, [this] { return !workItems_.empty(); });
          work = std::move(workItems_.front());
          workItems_.pop();
        }
        if (!work) {
          break;
        }
        work();
      }
    });
  }
}

TestExecutor::~TestExecutor() {
  for (auto& worker : workers_) {
    addImpl({});
  }

  for (auto& worker : workers_) {
    worker.join();
  }
}

void TestExecutor::add(Func f) {
  if (f) {
    addImpl(std::move(f));
  }
}

uint32_t TestExecutor::numThreads() const {
  return workers_.size();
}

void TestExecutor::addImpl(Func f) {
  {
    lock_guard<mutex> g(m_);
    workItems_.push(std::move(f));
  }
  cv_.notify_one();
}

} // folly
