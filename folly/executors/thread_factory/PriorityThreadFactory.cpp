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

#include <folly/executors/thread_factory/PriorityThreadFactory.h>

#include <glog/logging.h>
#include <folly/String.h>
#include <folly/portability/SysResource.h>
#include <folly/portability/SysTime.h>
#include <folly/system/ThreadName.h>

namespace folly {

PriorityThreadFactory::PriorityThreadFactory(
    std::shared_ptr<ThreadFactory> factory, int priority)
    : InitThreadFactory(std::move(factory), [priority] {
        if (setpriority(PRIO_PROCESS, 0, priority) == 0) {
          return;
        }
        int errnoCopy = errno;
        auto message = [&](std::ostream& os) {
          // Likely cause of failure is lacking the necessary permissions to
          // change thread priority; note that we may need higher permissions
          // even if trying to set the default priority while the current
          // priority is lower (niced), for example if the thread is spawned
          // from a lower-priority thread.
          os << "setpriority(" << priority << ") on thread \""
             << folly::getCurrentThreadName().value_or("<unknown>") << "\" "
             << "failed with error " << errnoCopy << " (" << errnoStr(errnoCopy)
             << ")";

          errno = 0;
          if (int p = getpriority(PRIO_PROCESS, 0); p != -1 || errno == 0) {
            os << ". Current priority: " << p;
          }
        };
        message(LOG(WARNING));
      }) {}

} // namespace folly
