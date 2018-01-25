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

#include <folly/Singleton.h>
#include <folly/ThreadLocal.h>
#include <folly/functional/Invoke.h>

namespace folly {

/// SingletonThreadLocal
///
/// Useful for a per-thread leaky-singleton model in libraries and applications.
///
/// By "leaky" it is meant that the T instances held by the instantiation
/// SingletonThreadLocal<T> will survive until their owning thread exits.
/// Therefore, they can safely be used before main() begins and after main()
/// ends, and they can also safely be used in an application that spawns many
/// temporary threads throughout its life.
///
/// Example:
///
///   struct UsefulButHasExpensiveCtor {
///     UsefulButHasExpensiveCtor(); // this is expensive
///     Result operator()(Arg arg);
///   };
///
///   Result useful(Arg arg) {
///     using Useful = UsefulButHasExpensiveCtor;
///     auto& useful = folly::SingletonThreadLocal<Useful>::get();
///     return useful(arg);
///   }
///
/// As an example use-case, the random generators in <random> are expensive to
/// construct. And their constructors are deterministic, but many cases require
/// that they be randomly seeded. So folly::Random makes good canonical uses of
/// folly::SingletonThreadLocal so that a seed is computed from the secure
/// random device once per thread, and the random generator is constructed with
/// the seed once per thread.
///
/// Keywords to help people find this class in search:
/// Thread Local Singleton ThreadLocalSingleton
template <
    typename T,
    typename Tag = detail::DefaultTag,
    typename Make = detail::DefaultMake<T>>
class SingletonThreadLocal {
 private:
  SingletonThreadLocal() = delete;

  struct Wrapper {
    // keep as first field, to save 1 instr in the fast path
    union {
      alignas(alignof(T)) unsigned char storage[sizeof(T)];
      T object;
    };
    Wrapper** cache{};

    /* implicit */ operator T&() {
      return object;
    }

    // normal make types
    template <
        typename S = T,
        _t<std::enable_if<is_invocable_r<S, Make>::value, int>> = 0>
    Wrapper() {
      (void)new (storage) S(Make{}());
    }
    // default and special make types for non-move-constructible T, until C++17
    template <
        typename S = T,
        _t<std::enable_if<!is_invocable_r<S, Make>::value, int>> = 0>
    Wrapper() {
      (void)Make{}(storage);
    }
    ~Wrapper() {
      if (cache) {
        *cache = nullptr;
      }
      object.~T();
    }
  };

  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static Wrapper& getWrapperInline() {
    static LeakySingleton<ThreadLocal<Wrapper>, Tag> singleton;
    return *singleton.get();
  }

  FOLLY_NOINLINE static Wrapper& getWrapperOutline() {
    return getWrapperInline();
  }

  /// Benchmarks indicate that getSlow being inline but containing a call to
  /// getWrapperOutline is faster than getSlow being outline but containing
  /// a call to getWrapperInline, which would otherwise produce smaller code.
  FOLLY_ALWAYS_INLINE static Wrapper& getSlow(Wrapper*& cache) {
    cache = &getWrapperOutline();
    cache->cache = &cache;
    return *cache;
  }

 public:
  FOLLY_EXPORT FOLLY_ALWAYS_INLINE static T& get() {
    // the absolute minimal conditional-compilation
#ifdef FOLLY_TLS
    static FOLLY_TLS Wrapper* cache;
    return FOLLY_LIKELY(!!cache) ? *cache : getSlow(cache);
#else
    return getWrapperInline();
#endif
  }
};
} // namespace folly
