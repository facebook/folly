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
  using Self = StaticSingletonManagerSansRtti;
  using Cache = std::atomic<void*>;
  using Instance = void*(bool);
  struct Arg {
    Cache cache{}; // should be first field
    Instance* instance;

    template <typename T, typename Tag>
    /* implicit */ constexpr Arg(tag_t<T, Tag>) noexcept
        : instance{instance_<T, Tag>} {}
  };

  template <typename T, typename Tag>
  static void* debug; // visible to debugger

 public:
  template <bool Noexcept>
  struct ArgCreate : private Arg {
    friend class StaticSingletonManagerSansRtti;

    template <typename T, typename Tag>
    /* implicit */ constexpr ArgCreate(tag_t<T, Tag> t) noexcept : Arg{t} {
      static_assert(Noexcept == noexcept(T()), "mismatched noexcept");
    }
  };

  /// get_existing_cached
  ///
  /// Returns a pointer to the global if it has already been created and if it
  /// is also already cached in the global arg.
  template <typename T, typename Tag>
  FOLLY_ERASE static T* get_existing_cached() {
    return get_existing_cached<T>(global<T, Tag>());
  }

  /// get_existing
  ///
  /// Returns a pointer to the global if it has already been created. Caches it
  /// in the global arg.
  template <typename T, typename Tag>
  FOLLY_ERASE static T* get_existing() {
    return get_existing<T>(global<T, Tag>());
  }

  /// create
  ///
  /// Returns a pointer to the global if it has already been created, or creates
  /// it. Caches it in the global arg.
  template <typename T, typename Tag>
  FOLLY_ERASE static T& create() {
    return create<T>(global<T, Tag>());
  }

  /// get_existing_cached
  ///
  /// Returns a pointer to the global if it has already been created and if it
  /// is also already cached in the given arg.
  template <typename T, typename..., bool Noexcept = noexcept(T())>
  FOLLY_ERASE static T* get_existing_cached(ArgCreate<Noexcept>& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    return static_cast<T*>(v);
  }

  /// get_existing
  ///
  /// Returns a pointer to the global if it has already been created. Caches it
  /// in the given arg.
  template <typename T, typename..., bool Noexcept = noexcept(T())>
  FOLLY_ERASE static T* get_existing(ArgCreate<Noexcept>& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    auto const p = FOLLY_LIKELY(!!v) ? v : get_existing_(arg);
    return static_cast<T*>(p);
  }

  /// create
  ///
  /// Returns a pointer to the global if it has already been created, or creates
  /// it. Caches it in the given arg.
  template <typename T, typename..., bool Noexcept = noexcept(T())>
  FOLLY_ERASE static T& create(ArgCreate<Noexcept>& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    auto const p = FOLLY_LIKELY(!!v) ? v : create_<noexcept(T())>(arg);
    return *static_cast<T*>(p);
  }

 private:
  template <typename T, typename Tag, typename R = ArgCreate<noexcept(T())>>
  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static FOLLY_CXX23_CONSTEXPR R& global() {
    static ArgCreate<noexcept(T())> arg{tag<T, Tag>};
    return arg;
  }

  template <typename T, typename Tag>
  FOLLY_EXPORT static void* instance_(bool create) {
    static_assert(std::atomic<void*>::is_always_lock_free);
    // the two static variables must be in the same function in order for them
    // to be exported together and therefore to be structurally in sync
    static std::atomic<void*> guard{nullptr};
    if (!create) {
      return guard.load(std::memory_order_acquire);
    } else {
      struct Holder {
        T instance;
        Holder() {
          auto const ptr = &reinterpret_cast<unsigned char&>(instance);
          guard.store(ptr, std::memory_order_release);
          debug<T, Tag> = ptr;
        }
      };
      static Indestructible<Holder> holder{};
      return &holder->instance;
    }
  }

  FOLLY_NOINLINE static void* get_existing_(Arg& arg) noexcept {
    auto const v = arg.instance(false);
    if (v) {
      arg.cache.store(v, std::memory_order_release);
    }
    return v;
  }

  template <bool Noexcept>
  FOLLY_NOINLINE static void* create_(Arg& arg) noexcept(Noexcept) {
    auto const v = arg.instance(true);
    arg.cache.store(v, std::memory_order_release);
    return v;
  }
};

template <typename T, typename Tag>
void* StaticSingletonManagerSansRtti::debug;

// This internal-use-only class is used to create all leaked Meyers singletons.
// It guarantees that only one instance of every such singleton will ever be
// created, even when requested from different compilation units linked
// dynamically.
//
// Supports dynamic loading but requires rtti.
class StaticSingletonManagerWithRtti {
 private:
  using Self = StaticSingletonManagerWithRtti;
  using Key = std::type_info;
  using Make = void*();
  using Cache = std::atomic<void*>;
  template <typename T, typename Tag>
  struct FOLLY_EXPORT Src {};
  struct Arg {
    Cache cache{}; // should be first field
    Key const* key;
    Make* make;
    void** debug;

    // gcc and clang behave poorly if typeid is hidden behind a non-constexpr
    // function, but typeid is not constexpr under msvc
    template <typename T, typename Tag>
    /* implicit */ constexpr Arg(tag_t<T, Tag>) noexcept
        : key{FOLLY_TYPE_INFO_OF(Src<T, Tag>)},
          make{thunk::make<T>},
          debug{&Self::debug<T, Tag>} {}
  };

  template <typename T, typename Tag>
  static void* debug; // visible to debugger

 public:
  template <bool Noexcept>
  struct ArgCreate : private Arg {
    friend class StaticSingletonManagerWithRtti;

    template <typename T, typename Tag>
    /* implicit */ constexpr ArgCreate(tag_t<T, Tag> t) noexcept : Arg{t} {
      static_assert(Noexcept == noexcept(T()), "mismatched noexcept");
    }
  };

  /// get_existing_cached
  ///
  /// Returns a pointer to the global if it has already been created and if it
  /// is also already cached in the global arg.
  template <typename T, typename Tag>
  FOLLY_ERASE static T* get_existing_cached() {
    return get_existing_cached<T>(global<T, Tag>());
  }

  /// get_existing
  ///
  /// Returns a pointer to the global if it has already been created. Caches it
  /// in the global arg.
  template <typename T, typename Tag>
  FOLLY_ERASE static T* get_existing() {
    return get_existing<T>(global<T, Tag>());
  }

  /// create
  ///
  /// Returns a pointer to the global if it has already been created, or creates
  /// it. Caches it in the global arg.
  template <typename T, typename Tag>
  FOLLY_ERASE static T& create() {
    return create<T>(global<T, Tag>());
  }

  /// get_existing_cached
  ///
  /// Returns a pointer to the global if it has already been created and if it
  /// is also already cached in the given arg.
  template <typename T, typename..., bool Noexcept = noexcept(T())>
  FOLLY_ERASE static T* get_existing_cached(ArgCreate<Noexcept>& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    return static_cast<T*>(v);
  }

  /// get_existing
  ///
  /// Returns a pointer to the global if it has already been created. Caches it
  /// in the given arg.
  template <typename T, typename..., bool Noexcept = noexcept(T())>
  FOLLY_ERASE static T* get_existing(ArgCreate<Noexcept>& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    auto const p = FOLLY_LIKELY(!!v) ? v : get_existing_(arg);
    return static_cast<T*>(p);
  }

  /// create
  ///
  /// Returns a pointer to the global if it has already been created, or creates
  /// it. Caches it in the given arg.
  template <typename T, typename..., bool Noexcept = noexcept(T())>
  FOLLY_ERASE static T& create(ArgCreate<Noexcept>& arg) {
    auto const v = arg.cache.load(std::memory_order_acquire);
    auto const p = FOLLY_LIKELY(!!v) ? v : create_<noexcept(T())>(arg);
    return *static_cast<T*>(p);
  }

 private:
  template <typename T, typename Tag, typename R = ArgCreate<noexcept(T())>>
  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static FOLLY_CXX23_CONSTEXPR R& global() {
    static ArgCreate<noexcept(T())> arg{tag<T, Tag>};
    return arg;
  }

  FOLLY_NOINLINE static void* get_existing_(Arg& arg) noexcept;

  template <bool Noexcept>
  FOLLY_NOINLINE static void* create_(Arg& arg) noexcept(Noexcept);
};

template <typename T, typename Tag>
void* StaticSingletonManagerWithRtti::debug;

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
