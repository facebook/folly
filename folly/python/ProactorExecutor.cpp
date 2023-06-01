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

#include <folly/python/ProactorExecutor.h>
#include <folly/python/executor_api.h> // @manual
#include <folly/python/import.h>

namespace folly {
namespace python {

FOLLY_CONSTINIT static import_cache import_folly__executor_{
    import_folly__executor, "import_folly__executor"};

ProactorExecutor::ProactorExecutor(int iocp)
    : iocp_(iocp), notification_cache_() {}

void ProactorExecutor::add(Func func) noexcept {
  {
    std::lock_guard<std::mutex> _{mutex_};
    queue_.push(std::move(func));
  }
  notify();
}

void ProactorExecutor::drive() noexcept {
  std::lock_guard<std::mutex> _{mutex_};
  if (!queue_.empty()) {
    invokeCatchingExns(
        "ProactorExecutor: task", std::exchange(queue_.front(), {}));
    queue_.pop();
  }
}

void ProactorExecutor::notify() {
  import_folly__executor_();
  uint64_t address = new_iocp_overlapped();
  notification_cache_.emplace(address);
  iocp_post_job(iocp_, address);
}

bool ProactorExecutor::pop(uint64_t address) {
  if (auto search = notification_cache_.find(address);
      search != notification_cache_.end()) {
    notification_cache_.erase(address);
    return true;
  }
  return false;
}

} // namespace python
} // namespace folly
