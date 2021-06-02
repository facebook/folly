/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#pragma once

#include <folly/Singleton.h>
#include <folly/concurrency/DeadlockDetector.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/ThreadPoolExecutor.h>

namespace folly {

class IOThreadPoolDeadlockDetectorObserver
    : public folly::ThreadPoolExecutor::Observer {
 public:
  IOThreadPoolDeadlockDetectorObserver(
      folly::DeadlockDetectorFactory* deadlockDetectorFactory,
      const std::string& name);
  void threadStarted(folly::ThreadPoolExecutor::ThreadHandle* h) override;
  void threadStopped(folly::ThreadPoolExecutor::ThreadHandle* h) override;

  static std::unique_ptr<IOThreadPoolDeadlockDetectorObserver> create(
      const std::string& name);

 private:
  const std::string name_;
  folly::DeadlockDetectorFactory* deadlockDetectorFactory_;
  folly::Synchronized<std::unordered_map<
      folly::ThreadPoolExecutor::ThreadHandle*,
      std::unique_ptr<folly::DeadlockDetector>>>
      detectors_;
};

} // namespace folly
