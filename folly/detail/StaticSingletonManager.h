/*
 * Copyright 2016-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <atomic>
#include <typeinfo>

#include <folly/CPortability.h>
#include <folly/Likely.h>
#include <folly/detail/Singleton.h>

namespace folly {
namespace detail {

// This internal-use-only class is used to create all leaked Meyers singletons.
// It guarantees that only one instance of every such singleton will ever be
// created, even when requested from different compilation units linked
// dynamically.
class StaticSingletonManager {
 public:
  template <typename T, typename Tag>
  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static T& create() {
    static Cache cache;
    auto const& key = typeid(TypeTuple<T, Tag>);
    auto const v = cache.load(std::memory_order_acquire);
    auto const p = FOLLY_LIKELY(!!v) ? v : create_(key, make<T>, cache);
    return *static_cast<T*>(p);
  }

 private:
  using Key = std::type_info;
  using Make = void*();
  using Cache = std::atomic<void*>;

  template <typename T>
  static void* make() {
    return new T();
  }

  FOLLY_NOINLINE static void* create_(Key const& key, Make& make, Cache& cache);
};

template <typename T, typename Tag>
FOLLY_ALWAYS_INLINE FOLLY_ATTR_VISIBILITY_HIDDEN T& createGlobal() {
  return StaticSingletonManager::create<T, Tag>();
}

} // namespace detail
} // namespace folly
