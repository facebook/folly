/*
 * Copyright 2017 Facebook, Inc.
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

namespace folly {

// SingletonThreadLocal
//
// This class can help you implement a per-thread leaky-singleton model within
// your application. Please read the usage block at the top of Singleton.h as
// the recommendations there are also generally applicable to this class.
//
// When we say this is "leaky" we mean that the T instances held by a
// SingletonThreadLocal<T> will survive until their owning thread exits,
// regardless of the lifetime of the singleton object holding them.  That
// means that they can be safely used during process shutdown, and
// that they can also be safely used in an application that spawns many
// temporary threads throughout its life.
//
// Keywords to help people find this class in search:
// Thread Local Singleton ThreadLocalSingleton
template <typename T, typename Tag = detail::DefaultTag>
class SingletonThreadLocal {
 public:
  using CreateFunc = std::function<T*(void)>;

  SingletonThreadLocal() : SingletonThreadLocal([]() { return new T(); }) {}

  explicit SingletonThreadLocal(CreateFunc createFunc)
      : singleton_([createFunc = std::move(createFunc)]() mutable {
          return new ThreadLocalT([createFunc =
                                       std::move(createFunc)]() mutable {
            return new Wrapper(std::unique_ptr<T>(createFunc()));
          });
        }) {}

  static T& get() {
#ifdef FOLLY_TLS
    if (UNLIKELY(*localPtr() == nullptr)) {
      *localPtr() = &(**SingletonT::get());
    }

    return **localPtr();
#else
    return **SingletonT::get();
#endif
  }

 private:
#ifdef FOLLY_TLS
  static T** localPtr() {
    static FOLLY_TLS T* localPtr = nullptr;
    return &localPtr;
  }
#endif

  class Wrapper {
   public:
    explicit Wrapper(std::unique_ptr<T> t) : t_(std::move(t)) {}

    ~Wrapper() {
#ifdef FOLLY_TLS
      *localPtr() = nullptr;
#endif
    }

    T& operator*() { return *t_; }

   private:
    std::unique_ptr<T> t_;
  };

  using ThreadLocalT = ThreadLocal<Wrapper>;
  using SingletonT = LeakySingleton<ThreadLocalT, Tag>;

  SingletonT singleton_;
};
} // namespace folly
