/*
 * Copyright 2015-present Facebook, Inc.
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
#include "folly/experimental/ThreadedRepeatingFunctionRunner.h"

#include <glog/logging.h>
#include <iostream>

namespace folly {

ThreadedRepeatingFunctionRunner::ThreadedRepeatingFunctionRunner() {}

ThreadedRepeatingFunctionRunner::~ThreadedRepeatingFunctionRunner() {
  stopAndWarn("ThreadedRepeatingFunctionRunner");
}

void ThreadedRepeatingFunctionRunner::stopAndWarn(
    const std::string& class_of_destructor) {
  if (stopImpl()) {
    LOG(ERROR)
        << "ThreadedRepeatingFunctionRunner::stop() should already have been "
        << "called, since the " << class_of_destructor << " destructor is now "
        << "running. This is unsafe because it means that its threads "
        << "may be accessing class state that was already destroyed "
        << "(e.g. derived class members, or members that were declared after "
        << "the " << class_of_destructor << ") .";
    stop();
  }
}

void ThreadedRepeatingFunctionRunner::stop() {
  stopImpl();
}

bool ThreadedRepeatingFunctionRunner::stopImpl() {
  {
    std::unique_lock<std::mutex> lock(stopMutex_);
    if (stopping_) {
      return false; // Do nothing if stop() is called twice.
    }
    stopping_ = true;
  }
  stopCv_.notify_all();
  for (auto& t : threads_) {
    t.join();
  }
  return true;
}

void ThreadedRepeatingFunctionRunner::add(
    RepeatingFn fn,
    std::chrono::milliseconds initialSleep) {
  threads_.emplace_back(
      &ThreadedRepeatingFunctionRunner::executeInLoop,
      this,
      std::move(fn),
      initialSleep);
}

bool ThreadedRepeatingFunctionRunner::waitFor(
    std::chrono::milliseconds duration) noexcept {
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + duration;
  std::unique_lock<std::mutex> lock(stopMutex_);
  stopCv_.wait_until(
      lock, deadline, [&] { return stopping_ || clock::now() > deadline; });
  return !stopping_;
}

void ThreadedRepeatingFunctionRunner::executeInLoop(
    RepeatingFn fn,
    std::chrono::milliseconds initialSleep) noexcept {
  auto duration = initialSleep;
  while (waitFor(duration)) {
    duration = fn();
  }
}

} // namespace folly
