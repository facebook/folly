/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <memory>
#include <folly/concurrency/DeadlockDetector.h>
#include <folly/executors/IOThreadPoolDeadlockDetectorObserver.h>

namespace folly {

IOThreadPoolDeadlockDetectorObserver::IOThreadPoolDeadlockDetectorObserver(
    DeadlockDetectorFactory* deadlockDetectorFactory, const std::string& name)
    : name_(name), deadlockDetectorFactory_(deadlockDetectorFactory) {}

void IOThreadPoolDeadlockDetectorObserver::registerEventBase(EventBase& evb) {
  if (!deadlockDetectorFactory_) {
    return;
  }

  evb.runInEventBaseThread([this, &evb] {
    auto tid = folly::getOSThreadID();
    auto name = name_ + ":" + folly::to<std::string>(tid);
    auto deadlockDetector = deadlockDetectorFactory_->create(&evb, name);
    detectors_.wlock()->insert_or_assign(&evb, std::move(deadlockDetector));
  });
}

void IOThreadPoolDeadlockDetectorObserver::unregisterEventBase(EventBase& evb) {
  if (!deadlockDetectorFactory_) {
    return;
  }

  detectors_.wlock()->erase(&evb);
}

/* static */ std::unique_ptr<IOThreadPoolDeadlockDetectorObserver>
IOThreadPoolDeadlockDetectorObserver::create(const std::string& name) {
  auto* deadlockDetectorFactory = DeadlockDetectorFactory::instance();
  return std::make_unique<IOThreadPoolDeadlockDetectorObserver>(
      deadlockDetectorFactory, name);
}

} // namespace folly
