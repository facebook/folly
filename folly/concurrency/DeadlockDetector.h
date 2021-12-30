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

#pragma once

#include <folly/Executor.h>
#include <folly/executors/QueueObserver.h>

namespace folly {
class DeadlockDetector {
 public:
  virtual ~DeadlockDetector() {}
};

class DeadlockDetectorFactory {
 public:
  virtual ~DeadlockDetectorFactory() {}
  virtual std::unique_ptr<DeadlockDetector> create(
      Executor* executor,
      const std::string& name,
      std::unique_ptr<WorkerProvider> threadIdCollector) = 0;
  static DeadlockDetectorFactory* instance();
};

using GetDeadlockDetectorFactoryInstance = DeadlockDetectorFactory*();
#if FOLLY_HAVE_WEAK_SYMBOLS
FOLLY_ATTR_WEAK GetDeadlockDetectorFactoryInstance
    get_deadlock_detector_factory_instance;
#else
constexpr GetDeadlockDetectorFactoryInstance*
    get_deadlock_detector_factory_instance = nullptr;
#endif
} // namespace folly
