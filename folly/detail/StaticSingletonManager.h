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

#pragma once

#include <atomic>
#include <typeinfo>

#include <folly/CPortability.h>
#include <folly/Indestructible.h>
#include <folly/Likely.h>
#include <folly/Utility.h>
#include <folly/detail/Singleton.h>
#include <folly/lang/Thunk.h>
#include <folly/lang/TypeInfo.h>

namespace folly {
namespace detail {

// Does not support dynamic loading but works without rtti.
class StaticSingletonManagerSansRtti {
 private:
  using Cache = std::atomic<void*>;
  using Make = void*();
  struct Arg {
    Cache cache{}; // should be first field
    Make* make;

    template <typename T, typename Tag>
    /* implicit */ constexpr Arg(tag_t<T, Tag>) noexcept
        : make{thunk::make<T>} {}
  };

  template <bool Noexcept>
  static void* create_(Arg&) noexcept(Noexcept); // no defn; only for decltype

  template <bool Noexcept>
  using Create = decltype(create_<Noexcept>);

 public:
  template <bool Noexcept>
  struct ArgCreate : private Arg {
    friend class StaticSingletonManagerSansRtti;

    template <typename T, typename Tag>
    /* implicit */ constexpr ArgCreate(tag_t<T, Tag> t) noexcept
        : Arg{t}, create{create_<T, Tag>} {
      static_assert(Noexcept == noexcept(T()), "mismatched noexcept");
    }

   private:
    Create<Noexcept>* create;
  };

  template <typename T, typename Tag>
  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static T& create() {
    static Arg arg{tag<T, Tag>};
    return create<T, Tag>(arg);
  }

  template <typename T, typename..., bool Noexcept = noexcept(T())>
  FOLLY_ERASE static T& create(ArgCreate<Noexcept>& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    auto const p = FOLLY_LIKELY(!!v) ? v : arg.create(arg);
    return *static_cast<T*>(p);
  }

 private:
  template <typename T, typename Tag>
  FOLLY_ERASE static T& create(Arg& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    auto const p = FOLLY_LIKELY(!!v) ? v : create_<T, Tag>(arg);
    return *static_cast<T*>(p);
  }

  template <typename T, typename Tag>
  FOLLY_EXPORT FOLLY_NOINLINE static void* create_(Arg& arg) noexcept(
      noexcept(T())) {
    auto& cache = arg.cache;
    static Indestructible<T> instance;
    cache.store(&*instance, std::memory_order_release);
    return &*instance;
  }
};

// This internal-use-only class is used to create all leaked Meyers singletons.
// It guarantees that only one instance of every such singleton will ever be
// created, even when requested from different compilation units linked
// dynamically.
//
// Supports dynamic loading but requires rtti.
class StaticSingletonManagerWithRtti {
 private:
  using Key = std::type_info;
  using Make = void*();
  using Cache = std::atomic<void*>;
  struct Arg {
    Cache cache{}; // should be first field
    Key const* key;
    Make* make;

    // gcc and clang behave poorly if typeid is hidden behind a non-constexpr
    // function, but typeid is not constexpr under msvc
    template <typename T, typename Tag>
    /* implicit */ constexpr Arg(tag_t<T, Tag>) noexcept
        : key{FOLLY_TYPE_INFO_OF(tag_t<T, Tag>)}, make{thunk::make<T>} {}
  };

  template <bool Noexcept>
  FOLLY_NOINLINE static void* create_(Arg&) noexcept(Noexcept);

  template <bool Noexcept>
  using Create = decltype(create_<Noexcept>);

 public:
  template <bool Noexcept>
  struct ArgCreate : private Arg {
    friend class StaticSingletonManagerWithRtti;

    template <typename T, typename Tag>
    /* implicit */ constexpr ArgCreate(tag_t<T, Tag> t) noexcept
        : Arg{t}, create{create_<Noexcept>} {
      static_assert(Noexcept == noexcept(T()), "mismatched noexcept");
    }

   private:
    Create<Noexcept>* create;
  };

  template <typename T, typename Tag>
  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static T& create() {
    static Arg arg{tag<T, Tag>};
    return create<T, Tag>(arg);
  }

  template <typename T, typename..., bool Noexcept = noexcept(T())>
  FOLLY_ERASE static T& create(ArgCreate<Noexcept>& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    auto const p = FOLLY_LIKELY(!!v) ? v : arg.create(arg);
    return *static_cast<T*>(p);
  }

 private:
  template <typename T, typename Tag>
  FOLLY_ERASE static T& create(Arg& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    auto const p = FOLLY_LIKELY(!!v) ? v : create_<noexcept(T())>(arg);
    return *static_cast<T*>(p);
  }
};

using StaticSingletonManager = std::conditional_t<
    kHasRtti,
    StaticSingletonManagerWithRtti,
    StaticSingletonManagerSansRtti>;

template <typename T, typename Tag>
FOLLY_ERASE T& createGlobal() {
  return StaticSingletonManager::create<T, Tag>();
}

} // namespace detail
} // namespace folly
