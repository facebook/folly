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

#include <fmt/core.h>

#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/python/error.h>

namespace folly {
namespace python {

/**
 * Call-once like abstraction for cython-api module imports.
 *
 * On failure, returns false and keeps python error indicator set (caller must
 * handle it).
 */
class import_cache_nocapture {
 public:
  using sig = int();

  FOLLY_CONSTEVAL import_cache_nocapture(sig& fun) noexcept : fun_{fun} {}

  bool operator()() const {
    // protecting this with the cxxabi mutex (the mutex that
    // guards static local variables) leads to deadlock with
    // cpython's gil; at-least-once semantics is fine here
    auto const val = fun_.load(std::memory_order_acquire);
    return FOLLY_LIKELY(!val) ? true : call_slow();
  }

 private:
  FOLLY_NOINLINE bool call_slow() const {
    auto const val = fun_.load(std::memory_order_acquire);
    if (val && 0 != val()) {
      return false;
    } else {
      fun_.store(nullptr, std::memory_order_release);
      return true;
    }
  }

 private:
  mutable std::atomic<sig*> fun_; // if nullptr, already called
};

/**
 * On failure, captures python exception (clearing error indicator) and throws a
 * wrapper C++ exception
 */
class import_cache {
 public:
  FOLLY_CONSTEVAL import_cache(
      import_cache_nocapture::sig& fun, char const* const name) noexcept
      : impl_{fun}, name_{name ? name : "<unknown>"} {}

  void operator()() const {
    if (!impl_()) {
      handlePythonError(fmt::format("import {} failed: ", name_));
    }
  }

 private:
  import_cache_nocapture impl_;
  char const* const name_; // for handling import errors
};

} // namespace python
} // namespace folly
