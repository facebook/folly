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

#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Traits.h>
#include <folly/coro/WithAsyncStack.h>
#include <folly/coro/detail/CurrentAsyncFrame.h>
#include <folly/coro/safe/NowTask.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {
namespace {

struct Err {};

// The parameters are `WithAsyncStackAwaiter::await_suspend`'s test matrix
template <bool IsNoexcept, bool ReturnsBool = false>
struct TestAwaiter {
  bool await_ready() noexcept { return false; }

  template <typename Promise>
  auto await_suspend(coroutine_handle<Promise>) noexcept(IsNoexcept) {
    if constexpr (!IsNoexcept) {
      throw Err{};
    }
    if constexpr (ReturnsBool) {
      return false; // Don't actually suspend
    }
  }

  void await_resume() noexcept {}

  // Verify wrapped awaiter noexcept matches inner awaiter noexcept.
  static constexpr void check_wrapped() {
    using Wrapped = decltype(get_awaiter(co_withAsyncStack(TestAwaiter{})));
    static_assert(
        IsNoexcept ==
        noexcept(std::declval<Wrapped&>().await_suspend(
            coroutine_handle<now_task<int>::promise_type>{})));
    static_assert(
        std::is_same_v<Wrapped, detail::WithAsyncStackAwaiter<TestAwaiter>>);
  }
};

CO_TEST(WithAsyncStackTest, AwaitSuspendNoexceptPropagates) {
  // Wrapped awaiter is noexcept iff inner awaiter is noexcept.
  TestAwaiter</*IsNoexcept=*/true>::check_wrapped();
  TestAwaiter</*IsNoexcept=*/false>::check_wrapped();
  TestAwaiter</*IsNoexcept=*/true, /*ReturnsBool=*/true>::check_wrapped();
  TestAwaiter</*IsNoexcept=*/false, /*ReturnsBool=*/true>::check_wrapped();

  auto& frameBefore = co_await detail::co_current_async_stack_frame;

  // Exception from inner await_suspend propagates, and async stack is restored.
  EXPECT_THROW(co_await TestAwaiter</*IsNoexcept=*/false>{}, Err);

  // Async stack frame should be restored after the exception.
  auto& frameAfter = co_await detail::co_current_async_stack_frame;
  EXPECT_EQ(&frameBefore, &frameAfter);
}

} // namespace
} // namespace folly::coro

#endif // FOLLY_HAS_COROUTINES
