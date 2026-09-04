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

#include <folly/Executor.h>
#include <folly/coro/Coroutine.h>
#include <folly/coro/WithAsyncStack.h>
#include <folly/tracing/AsyncStack.h>

#include <array>
#include <utility>
#include <vector>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

class AsyncStackTraceAwaitable {
  class Awaiter {
   public:
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    bool await_suspend(coroutine_handle<Promise> h) noexcept {
      initialFrame_ = &h.promise().getAsyncFrame();
      return false;
    }

    FOLLY_NOINLINE std::vector<std::uintptr_t> await_resume() {
      // Only the written prefix of `result` is read; avoid zero-filling it.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
      std::array<std::uintptr_t, 100> result;

      result[0] =
          reinterpret_cast<std::uintptr_t>(FOLLY_ASYNC_STACK_RETURN_ADDRESS());
      const auto numFrames = getAsyncStackTraceFromInitialFrame(
          initialFrame_, result.data() + 1, result.size() - 1);

      return std::vector<std::uintptr_t>(
          std::make_move_iterator(result.begin()),
          std::make_move_iterator(result.begin()) + numFrames + 1);
    }

   private:
    folly::AsyncStackFrame* initialFrame_;
  };

 public:
  AsyncStackTraceAwaitable viaIfAsync(
      const folly::Executor::KeepAlive<>&) const noexcept {
    return {};
  }

  Awaiter operator co_await() const noexcept { return {}; }

  friend AsyncStackTraceAwaitable tag_invoke(
      cpo_t<co_withAsyncStack>, AsyncStackTraceAwaitable awaitable) noexcept {
    return awaitable;
  }
};

inline constexpr AsyncStackTraceAwaitable co_current_async_stack_trace = {};

struct AsyncStackTraceEntry {
  std::uintptr_t address{};
  folly::AsyncStackMetadata metadata{};
};

class AsyncStackTraceWithMetadataAwaitable {
 private:
  class Awaiter {
   public:
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    bool await_suspend(coroutine_handle<Promise> h) noexcept {
      initialFrame_ = &h.promise().getAsyncFrame();
      return false;
    }

    FOLLY_NOINLINE std::vector<AsyncStackTraceEntry> await_resume() {
      // Only the written prefix of `addresses` is read; avoid zero-filling it.
      constexpr size_t kMaxAddresses = 100;
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
      std::array<std::uintptr_t, kMaxAddresses> addresses;
      // Default construction leaves the current instruction's metadata empty.
      std::array<folly::AsyncStackMetadata, kMaxAddresses> metadata;

      addresses[0] =
          reinterpret_cast<std::uintptr_t>(FOLLY_ASYNC_STACK_RETURN_ADDRESS());
      const auto numFrames = getAsyncStackTraceFromInitialFrameWithMetadata(
          initialFrame_,
          addresses.data() + 1,
          metadata.data() + 1,
          addresses.size() - 1);

      std::vector<AsyncStackTraceEntry> result;
      result.reserve(numFrames + 1);
      for (size_t i = 0; i < numFrames + 1; ++i) {
        result.emplace_back(addresses[i], metadata[i]);
      }
      return result;
    }

   private:
    folly::AsyncStackFrame* initialFrame_;
  };

 public:
  AsyncStackTraceWithMetadataAwaitable viaIfAsync(
      const folly::Executor::KeepAlive<>&) const noexcept {
    return {};
  }

  Awaiter operator co_await() const noexcept { return {}; }

  friend AsyncStackTraceWithMetadataAwaitable tag_invoke(
      cpo_t<co_withAsyncStack>,
      AsyncStackTraceWithMetadataAwaitable awaitable) noexcept {
    return awaitable;
  }
};

// Entries are ordered from leaf to root. The first entry describes the
// currently executing instruction and has no metadata. Metadata from
// `co_withMetadata` is attached to the visible wrapper entry.
inline constexpr AsyncStackTraceWithMetadataAwaitable
    co_current_async_stack_trace_with_metadata = {};

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
