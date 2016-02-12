/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/futures/ThreadedExecutor.h>

#include <glog/logging.h>

using namespace std;

namespace folly { namespace futures {

ThreadedExecutor::~ThreadedExecutor() {
  lock_guard<mutex> lock(mutex_);
  destructing_ = true;
  for (auto& th : threads_) {
    th.join();
  }
}

void ThreadedExecutor::add(Func f) {
  lock_guard<mutex> lock(mutex_);
  CHECK(!destructing_);
  threads_.emplace_back(std::move(f));
}

}}
