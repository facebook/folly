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

#include <folly/executors/thread_factory/InitThreadFactory.h>

namespace folly {

/**
 * A ThreadFactory that sets nice values for each thread.  The main
 * use case for this class is if there are multiple
 * CPUThreadPoolExecutors in a single process, or between multiple
 * processes, where some should have a higher priority than the others.
 *
 * Note that per-thread nice values are not POSIX standard, but both
 * pthreads and linux support per-thread nice.  The default linux
 * scheduler uses these values to do smart thread prioritization.
 * sched_priority function calls only affect real-time schedulers.
 */
class PriorityThreadFactory : public InitThreadFactory {
 public:
  PriorityThreadFactory(std::shared_ptr<ThreadFactory> factory, int priority);
};

} // namespace folly
