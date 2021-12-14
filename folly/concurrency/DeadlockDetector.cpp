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

#include <folly/concurrency/DeadlockDetector.h>

namespace folly {
/* static */ DeadlockDetectorFactory* DeadlockDetectorFactory::instance() {
  if (get_deadlock_detector_factory_instance) {
    return get_deadlock_detector_factory_instance();
  }

  return nullptr;
}
} // namespace folly
