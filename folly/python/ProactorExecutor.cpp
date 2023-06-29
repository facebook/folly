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

namespace folly {
namespace python {

ProactorExecutor::ProactorExecutor(uint64_t iocp) : iocp_(iocp), cache_() {}

void ProactorExecutor::add(Func func) noexcept {
  auto callback =
      std::make_unique<detail::ProactorExecutorCallback>(std::move(func));
  auto address = callback->address();
  cache_.emplace(address, std::move(callback)).first->second->send(iocp_);
}

void ProactorExecutor::drive() noexcept {
  while (!cache_.empty()) {
    auto item = cache_.begin();
    item->second->invoke();
    cache_.erase(item->first);
  }
}

bool ProactorExecutor::execute(uintptr_t address) {
  if (auto search = cache_.find(address); search != cache_.end()) {
    search->second->invoke();
    cache_.erase(address);
    return true;
  }
  return false;
}

} // namespace python
} // namespace folly
