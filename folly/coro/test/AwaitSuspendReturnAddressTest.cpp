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

// Regression test for the FOLLY_NOINLINE on Task<T>::Awaiter::await_suspend.
//
// await_suspend() records the async-stack "splice" instruction pointer via
// AsyncStackFrame::setReturnAddress(), which defaults to
// __builtin_return_address(0). Because await_suspend runs in the compiler-
// generated resume funclet of the *awaiting* coroutine, that return address
// points at the co_await site inside the awaiting coroutine -- which is what
// async-stack tooling reports as the awaited coroutine's caller.
//
// If await_suspend is inlined (NOINLINE removed), __builtin_return_address(0)
// captures a wrong-but-nonzero address that resolves into
// folly::resumeCoroutineWithNewAsyncStackRoot instead of the co_await site.
// The existing folly/coro async-stack tests only assert non-nullness and stack
// structure, so they miss this. This test symbolizes the captured IP and
// asserts it resolves into the awaiting coroutine.

#include <folly/Portability.h>

#include <folly/Demangle.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/debugging/symbolizer/SymbolizedFrame.h>
#include <folly/debugging/symbolizer/Symbolizer.h>
#include <folly/tracing/AsyncStack.h>

#include <string>

#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

#if FOLLY_HAS_COROUTINES

using namespace folly::coro;
using folly::symbolizer::LocationInfoMode;
using folly::symbolizer::SymbolizedFrame;
using folly::symbolizer::Symbolizer;

namespace {

// Resolve an instruction pointer to a demangled symbol name (or a sentinel).
std::string symbolizeName(void* ip) {
  Symbolizer symbolizer(LocationInfoMode::FULL);
  SymbolizedFrame frame;
  if (ip == nullptr ||
      !symbolizer.symbolize(reinterpret_cast<uintptr_t>(ip), frame) ||
      !frame.found || frame.name == nullptr) {
    return "<unresolved>";
  }
  return folly::demangle(frame.name).toStdString();
}

bool nameIsAwaitingFn(const std::string& name) {
  return name.find("awaitingFn") != std::string::npos;
}

// The awaited callee. When its body runs, the top async-stack frame is this
// coroutine's own frame, whose instruction pointer was set by
// Task<T>::Awaiter::await_suspend to the return address of the awaiting
// coroutine's resume funclet -- i.e. the co_await site inside awaitingFn().
Task<void*> calleeTask() {
  auto* top = folly::getCurrentAsyncStackRoot().getTopFrame();
  CHECK(top != nullptr);
  co_return top->getReturnAddress();
}

// The awaiting coroutine. The co_await below is the source location the
// captured IP must resolve to. Marked NOINLINE so its resume funclet is a
// distinct, symbolizable function regardless of build mode.
FOLLY_NOINLINE Task<void*> awaitingFn() {
  co_return co_await calleeTask();
}

// An unrelated function, used to prove the name check is not vacuous.
FOLLY_NOINLINE void unrelatedFunction() {
  asm volatile("" ::: "memory");
}

} // namespace

TEST(AwaitSuspendReturnAddressTest, IpResolvesIntoAwaitingFn) {
  SKIP_IF(!Symbolizer::isAvailable());

  void* ip = blockingWait(awaitingFn());
  ASSERT_NE(ip, nullptr);

  const std::string name = symbolizeName(ip);
  EXPECT_TRUE(nameIsAwaitingFn(name))
      << "await_suspend IP resolved to: " << name
      << " (expected to contain 'awaitingFn')";

  // Guard against a vacuous match: an unrelated function's address must not
  // satisfy the same check.
  EXPECT_FALSE(nameIsAwaitingFn(
      symbolizeName(reinterpret_cast<void*>(&unrelatedFunction))));
}

#endif // FOLLY_HAS_COROUTINES
