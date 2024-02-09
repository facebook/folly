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

FOLLY_NOINLINE SingletonThreadLocalState::Wrapper::Wrapper() noexcept {}

FOLLY_NOINLINE SingletonThreadLocalState::Wrapper::~Wrapper() {
  for (auto& kvp : caches) {
    kvp.first->cache = nullptr;
  }
}

FOLLY_NOINLINE void SingletonThreadLocalState::LocalLifetime::destroy(
    Wrapper& wrapper) noexcept {
  auto& lifetimes = wrapper.lifetimes[this];
  for (auto cache : lifetimes) {
    auto const it = wrapper.caches.find(cache);
    if (!--it->second) {
      wrapper.caches.erase(it);
      cache->cache = nullptr;
    }
  }
  wrapper.lifetimes.erase(this);
}

FOLLY_NOINLINE void SingletonThreadLocalState::LocalLifetime::track(
    LocalCache& cache, Wrapper& wrapper, void* object) noexcept {
  cache.cache = object;
  auto const inserted = wrapper.lifetimes[this].insert(&cache);
  wrapper.caches[&cache] += inserted.second;
}

} // namespace detail

} // namespace folly
