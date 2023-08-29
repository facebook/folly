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

#include <folly/experimental/symbolizer/test/SignalHandlerTest.h>

#include <folly/experimental/symbolizer/SignalHandler.h>

#include <folly/CPortability.h>
#include <folly/FileUtil.h>
#include <folly/Range.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>
#include <folly/portability/GTest.h>

#include <glog/logging.h>

#include <memory>

namespace folly {
namespace symbolizer {
namespace test {

namespace {

void print(StringPiece sp) {
  writeFull(STDERR_FILENO, sp.data(), sp.size());
}

void callback1() {
  print("Callback1\n");
}

void callback2() {
  if (fatalSignalReceived()) {
    print("Callback2\n");
  }
}

[[noreturn]] FOLLY_NOINLINE void funcC() {
  LOG(FATAL) << "Die";
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcB() {
  funcC();
  co_return;
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcA() {
  co_await co_funcB();
}

} // namespace

TEST(SignalHandler, Simple) {
  addFatalSignalCallback(callback1);
  addFatalSignalCallback(callback2);
  installFatalSignalHandler();
  installFatalSignalCallbacks();

  EXPECT_FALSE(fatalSignalReceived());

  EXPECT_DEATH(
      failHard(),
      "^\\*\\*\\* Aborted at [0-9]+ \\(Unix time, try 'date -d @[0-9]+'\\) "
      "\\*\\*\\*\n"
      "\\*\\*\\* Signal 11 \\(SIGSEGV\\) \\(0x2a\\) received by PID [0-9]+ "
      "\\(pthread TID 0x[0-9a-f]+\\) \\(linux TID [0-9]+\\) "
      "\\(code: address not mapped to object\\), "
      "stack trace: \\*\\*\\*\n"
      ".*\n"
      ".*    @ [0-9a-f]+.* folly::symbolizer::test::SignalHandler_Simple_Test"
      "::TestBody\\(\\).*\n"
      ".*\n"
      ".*    @ [0-9a-f]+.* main.*\n"
      ".*\n"
      "Callback1\n"
      "Callback2\n"
      ".*");
}

TEST(SignalHandler, AsyncStackTraceSimple) {
  addFatalSignalCallback(callback1);
  addFatalSignalCallback(callback2);
  installFatalSignalHandler();
  installFatalSignalCallbacks();

  EXPECT_DEATH(
      folly::coro::blockingWait(co_funcA()),
      "\\*\\*\\* Aborted at [0-9]+ \\(Unix time, try 'date -d @[0-9]+'\\) "
      "\\*\\*\\*\n"
      "\\*\\*\\* Signal 6 \\(SIGABRT\\) \\(0x[0-9a-f]+\\) received by PID [0-9]+ "
      "\\(pthread TID 0x[0-9a-f]+\\) \\(linux TID [0-9]+\\) .*, "
      "stack trace: \\*\\*\\*\n"
      ".*\n"
      ".*    @ [0-9a-f]+.* folly::symbolizer::test::SignalHandler"
      "_AsyncStackTraceSimple_Test::TestBody\\(\\).*\n"
      ".*\n"
      ".*    @ [0-9a-f]+.* main.*\n"
      ".*\n"
      "\\*\\*\\* Check failure async stack trace: \\*\\*\\*\n"
      "\\*\\*\\* First async stack root.* \\*\\*\\*\n"
      "\\*\\*\\* First async stack frame pointer.* \\*\\*\\*\n"
      ".*\n"
      ".*    @ [0-9a-f]+.* folly::symbolizer::test::\\(anonymous namespace\\)"
      "::co_funcA.*\n"
      ".*\n"
      "Callback1\n"
      "Callback2\n"
      ".*");
}

TEST(SignalHandler, AsyncStackTraceSimple2) {
  addFatalSignalCallback(callback1);
  addFatalSignalCallback(callback2);
  installFatalSignalHandler();
  installFatalSignalCallbacks();
  EXPECT_DEATH(
      [] {
        auto ex = std::make_unique<IOThreadPoolExecutor>(/* nThreads */ 1);
        auto fut = co_funcA().scheduleOn(ex.get()).start();
        fut.wait();
      }(),
      "\\*\\*\\* Aborted at [0-9]+ \\(Unix time, try 'date -d @[0-9]+'\\) "
      "\\*\\*\\*\n"
      "\\*\\*\\* Signal 6 \\(SIGABRT\\) \\(0x[0-9a-f]+\\) received by PID [0-9]+ "
      "\\(pthread TID 0x[0-9a-f]+\\) \\(linux TID [0-9]+\\) .*, "
      "stack trace: \\*\\*\\*\n"
      "(.*WARNING.*\n)?"
      ".*\n"
      ".*    @ [0-9a-f]+.* folly::symbolizer::test::\\(anonymous namespace\\)"
      "::funcC\\(\\)\n"
      ".*\n"
      "\\*\\*\\* Check failure async stack trace: \\*\\*\\*\n"
      "\\*\\*\\* First async stack root.* \\*\\*\\*\n"
      "\\*\\*\\* First async stack frame pointer.* \\*\\*\\*\n"
      ".*\n"
      ".*    @ [0-9a-f]+.* folly::symbolizer::test::\\(anonymous namespace\\)"
      "::co_funcA.*\n"
      ".*\n"
      "Callback1\n"
      "Callback2\n"
      ".*");
}

} // namespace test
} // namespace symbolizer
} // namespace folly

// Can't use initFacebookLight since that would install its own signal handlers
// Can't use initFacebookNoSignals since we cannot depend on common
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
