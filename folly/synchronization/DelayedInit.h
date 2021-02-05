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

#include <initializer_list>
#include <new>
#include <stdexcept>
#include <type_traits>

#include <folly/lang/SafeAssert.h>
#include <folly/synchronization/CallOnce.h>

namespace folly {

/**
 * DelayedInit -- thread-safe delayed initialization of a value. There are two
 * important differences between Lazy and DelayedInit:
 *   1. DelayedInit does not store the factory function inline.
 *   2. DelayedInit is thread-safe.
 *
 * Due to these differences, DelayedInit is suitable for data members. Lazy is
 * best for local stack variables.
 *
 * Example Usage:
 *
 *   struct Foo {
 *     Bar& bar() {
 *       LargeState state;
 *       return bar_.try_emplace_with(
 *           [this, &state] { return computeBar(state); });
 *     }
 *    private:
 *     Bar computeBar(LargeState&);
 *     DelayedInit<Bar> bar_;
 *   };
 *
 * If the above example were to use Lazy instead of DelayedInit:
 *   - Storage for LargeState and this-pointer would need to be reserved in the
 *     struct which wastes memory.
 *   - It would require additional synchronization logic for thread-safety.
 *
 *
 * Rationale:
 *
 *   - The stored value is initialized at most once and never deinitialized.
 *     Unlike Lazy, the initialization logic must be provided by the consumer.
 *     This means that DelayedInit is more of a "storage" type like
 *     std::optional. These semantics are perfect for thread-safe, lazy
 *     initialization of a data member.
 *
 *   - DelayedInit models neither MoveConstructible nor CopyConstructible. The
 *     rationale is the same as that of std::once_flag.
 *
 *   - There is no need for a non-thread-safe version of DelayedInit.
 *     std::optional will suffice in these cases.
 */
template <typename T>
struct DelayedInit {
  DelayedInit() = default;
  DelayedInit(const DelayedInit&) = delete;
  DelayedInit& operator=(const DelayedInit&) = delete;

  /**
   * Gets the pre-existing value if already initialized or creates the value
   * returned by the provided factory function. If the value already exists,
   * then the provided function is not called.
   */
  template <typename Func>
  T& try_emplace_with(Func func) {
    auto addr = static_cast<void*>(std::addressof(storage_.value));
    call_once(storage_.init, [&] { ::new (addr) T(func()); });
    return storage_.value;
  }

  /**
   * Gets the pre-existing value if already initialized or constructs the value
   * in-place by direct-initializing with the provided arguments.
   */
  template <typename... A>
  T& try_emplace(A&&... a) {
    return try_emplace_with([&] { return T(static_cast<A&&>(a)...); });
  }
  template <
      typename U,
      typename... A,
      typename = std::enable_if_t<
          std::is_constructible<T, std::initializer_list<U>, A...>::value>>
  T& try_emplace(std::initializer_list<U> ilist, A&&... a) {
    return try_emplace_with([&] { return T(ilist, static_cast<A&&>(a)...); });
  }

  bool has_value() const { return test_once(storage_.init); }
  explicit operator bool() const { return has_value(); }

  T& value() {
    require_value();
    return storage_.value;
  }

  const T& value() const {
    require_value();
    return storage_.value;
  }

  T& operator*() {
    FOLLY_SAFE_DCHECK(has_value(), "tried to access empty DelayedInit");
    return storage_.value;
  }
  const T& operator*() const {
    FOLLY_SAFE_DCHECK(has_value(), "tried to access empty DelayedInit");
    return storage_.value;
  }
  T* operator->() {
    FOLLY_SAFE_DCHECK(has_value(), "tried to access empty DelayedInit");
    return std::addressof(storage_.value);
  }
  const T* operator->() const {
    FOLLY_SAFE_DCHECK(has_value(), "tried to access empty DelayedInit");
    return std::addressof(storage_.value);
  }

 private:
  void require_value() const {
    if (!has_value()) {
      throw_exception<std::logic_error>("tried to access empty DelayedInit");
    }
  }

  using OnceFlag = std::conditional_t<
      alignof(T) >= sizeof(once_flag),
      once_flag,
      compact_once_flag>;

  struct StorageTriviallyDestructible {
    union {
      std::remove_const_t<T> value;
    };
    OnceFlag init;

    StorageTriviallyDestructible() {}
  };

  struct StorageNonTriviallyDestructible {
    union {
      std::remove_const_t<T> value;
    };
    OnceFlag init;

    StorageNonTriviallyDestructible() {}
    ~StorageNonTriviallyDestructible() {
      if (test_once(this->init)) {
        this->value.~T();
      }
    }
  };

  using Storage = std::conditional_t<
      std::is_trivially_destructible<T>::value,
      StorageTriviallyDestructible,
      StorageNonTriviallyDestructible>;

  Storage storage_;
};

} // namespace folly
