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

#include <folly/SingletonThreadLocal.h>

namespace folly {

namespace detail {

FOLLY_NOINLINE SingletonThreadLocalState::Tracking::Tracking() noexcept {}

FOLLY_NOINLINE SingletonThreadLocalState::Tracking::~Tracking() {
  for (auto& kvp : caches) {
    kvp.first->object = nullptr;
  }
}

FOLLY_NOINLINE void SingletonThreadLocalState::LocalLifetime::destroy(
    Tracking& tracking) noexcept {
  auto& lifetimes = tracking.lifetimes[this];
  for (auto cache : lifetimes) {
    auto const it = tracking.caches.find(cache);
    if (!--it->second) {
      tracking.caches.erase(it);
      cache->object = nullptr;
    }
  }
  tracking.lifetimes.erase(this);
}

FOLLY_NOINLINE void SingletonThreadLocalState::LocalLifetime::track(
    LocalCache& cache, Tracking& tracking, void* object) noexcept {
  cache.object = object;
  auto const inserted = tracking.lifetimes[this].insert(&cache);
  tracking.caches[&cache] += inserted.second;
}

} // namespace detail

} // namespace folly
