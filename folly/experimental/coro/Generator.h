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

#include <cassert>
#include <exception>
#include <experimental/coroutine>
#include <type_traits>
#include <utility>

#include <folly/experimental/coro/Invoke.h>

namespace folly {
namespace coro {

template <typename T>
class Generator {
 public:
  class promise_type final {
   public:
    promise_type() noexcept
        : m_value(nullptr),
          m_exception(nullptr),
          m_root(this),
          m_parentOrLeaf(this) {}

    promise_type(const promise_type&) = delete;
    promise_type(promise_type&&) = delete;

    auto get_return_object() noexcept {
      return Generator<T>{*this};
    }

    std::experimental::suspend_always initial_suspend() noexcept {
      return {};
    }

    std::experimental::suspend_always final_suspend() noexcept {
      return {};
    }

    void unhandled_exception() noexcept {
      m_exception = std::current_exception();
    }

    void return_void() noexcept {}

    std::experimental::suspend_always yield_value(T& value) noexcept {
      m_value = std::addressof(value);
      return {};
    }

    std::experimental::suspend_always yield_value(T&& value) noexcept {
      m_value = std::addressof(value);
      return {};
    }

    auto yield_value(Generator&& generator) noexcept {
      return yield_value(generator);
    }

    auto yield_value(Generator& generator) noexcept {
      struct awaitable {
        awaitable(promise_type* childPromise) : m_childPromise(childPromise) {}

        bool await_ready() noexcept {
          return this->m_childPromise == nullptr;
        }

        void await_suspend(
            std::experimental::coroutine_handle<promise_type>) noexcept {}

        void await_resume() {
          if (this->m_childPromise != nullptr) {
            this->m_childPromise->throw_if_exception();
          }
        }

       private:
        promise_type* m_childPromise;
      };

      if (generator.m_promise != nullptr) {
        m_root->m_parentOrLeaf = generator.m_promise;
        generator.m_promise->m_root = m_root;
        generator.m_promise->m_parentOrLeaf = this;
        generator.m_promise->resume();

        if (!generator.m_promise->is_complete()) {
          return awaitable{generator.m_promise};
        }

        m_root->m_parentOrLeaf = this;
      }

      return awaitable{nullptr};
    }

    // Don't allow any use of 'co_await' inside the Generator
    // coroutine.
    template <typename U>
    std::experimental::suspend_never await_transform(U&& value) = delete;

    void destroy() noexcept {
      std::experimental::coroutine_handle<promise_type>::from_promise(*this)
          .destroy();
    }

    void throw_if_exception() {
      if (m_exception != nullptr) {
        std::rethrow_exception(std::move(m_exception));
      }
    }

    bool is_complete() noexcept {
      return std::experimental::coroutine_handle<promise_type>::from_promise(
                 *this)
          .done();
    }

    T& value() noexcept {
      assert(this == m_root);
      assert(!is_complete());
      return *(m_parentOrLeaf->m_value);
    }

    void pull() noexcept {
      assert(this == m_root);
      assert(!m_parentOrLeaf->is_complete());

      m_parentOrLeaf->resume();

      while (m_parentOrLeaf != this && m_parentOrLeaf->is_complete()) {
        m_parentOrLeaf = m_parentOrLeaf->m_parentOrLeaf;
        m_parentOrLeaf->resume();
      }
    }

   private:
    void resume() noexcept {
      std::experimental::coroutine_handle<promise_type>::from_promise(*this)
          .resume();
    }

    std::add_pointer_t<T> m_value;
    std::exception_ptr m_exception;

    promise_type* m_root;

    // If this is the promise of the root generator then this field
    // is a pointer to the leaf promise.
    // For non-root generators this is a pointer to the parent promise.
    promise_type* m_parentOrLeaf;
  };

  Generator() noexcept : m_promise(nullptr) {}

  Generator(promise_type& promise) noexcept : m_promise(&promise) {}

  Generator(Generator&& other) noexcept : m_promise(other.m_promise) {
    other.m_promise = nullptr;
  }

  Generator(const Generator& other) = delete;
  Generator& operator=(const Generator& other) = delete;

  ~Generator() {
    if (m_promise != nullptr) {
      m_promise->destroy();
    }
  }

  Generator& operator=(Generator&& other) noexcept {
    if (this != &other) {
      if (m_promise != nullptr) {
        m_promise->destroy();
      }

      m_promise = other.m_promise;
      other.m_promise = nullptr;
    }

    return *this;
  }

  class iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    // What type should we use for counting elements of a potentially infinite
    // sequence?
    using difference_type = std::ptrdiff_t;
    using value_type = std::remove_reference_t<T>;
    using reference = std::conditional_t<std::is_reference_v<T>, T, T&>;
    using pointer = std::add_pointer_t<T>;

    iterator() noexcept : m_promise(nullptr) {}

    explicit iterator(promise_type* promise) noexcept : m_promise(promise) {}

    bool operator==(const iterator& other) const noexcept {
      return m_promise == other.m_promise;
    }

    bool operator!=(const iterator& other) const noexcept {
      return m_promise != other.m_promise;
    }

    iterator& operator++() {
      assert(m_promise != nullptr);
      assert(!m_promise->is_complete());

      m_promise->pull();
      if (m_promise->is_complete()) {
        auto* temp = m_promise;
        m_promise = nullptr;
        temp->throw_if_exception();
      }

      return *this;
    }

    void operator++(int) {
      (void)operator++();
    }

    reference operator*() const noexcept {
      assert(m_promise != nullptr);
      return static_cast<reference>(m_promise->value());
    }

    pointer operator->() const noexcept {
      return std::addressof(operator*());
    }

   private:
    promise_type* m_promise;
  };

  iterator begin() {
    if (m_promise != nullptr) {
      m_promise->pull();
      if (!m_promise->is_complete()) {
        return iterator(m_promise);
      }

      m_promise->throw_if_exception();
    }

    return iterator(nullptr);
  }

  iterator end() noexcept {
    return iterator(nullptr);
  }

  void swap(Generator& other) noexcept {
    std::swap(m_promise, other.m_promise);
  }

  template <typename F, typename... A, typename F_, typename... A_>
  friend Generator folly_co_invoke(tag_t<Generator, F, A...>, F_ f, A_... a) {
    auto&& r = invoke(static_cast<F&&>(f), static_cast<A&&>(a)...);
    for (auto&& v : r) {
      co_yield std::move(v);
    }
  }

 private:
  friend class promise_type;

  promise_type* m_promise;
};

template <typename T>
void swap(Generator<T>& a, Generator<T>& b) noexcept {
  a.swap(b);
}
} // namespace coro
} // namespace folly
