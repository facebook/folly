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

#include <cassert>
#include <iostream>
#include <folly/Executor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Task.h>
#include <folly/futures/Future.h>

class LoggingExecutor : public folly::Executor {
 public:
  void add(folly::Func f) override {
    std::cout << "- Adding work!\n";
    e_.add(std::move(f));
  }

  void step() { e_.step(); }

 private:
  folly::ManualExecutor e_;
};

folly::coro::Task<int> baz() {
  co_return 10;
}

folly::coro::Task<int> foo(folly::coro::Baton& b, int x) {
  std::cout << "- foo(" << x << ")\n";
  int inc = co_await baz();
  std::cout << "- baz() returns an increment of " << inc << "\n";
  co_await b;
  int ret = x + inc;
  std::cout << "- baton has been posted, foo(" << x << ") returning " << ret
            << "\n";
  co_return ret;
}

int bar(int x) {
  int ret = x + 10;
  std::cout << "- bar(" << x << ") returning " << ret << "\n";
  return ret;
}

int main() {
  LoggingExecutor le;
  auto executor = folly::Executor::getKeepAliveToken(le);
  folly::coro::Baton baton;

  {
    std::cout << "1. Making a Task.\n";
    folly::coro::Task t = foo(baton, 111);

    std::cout << "2. Assigning an Executor.\n";
    folly::coro::TaskWithExecutor te = std::move(t).scheduleOn(executor);

    std::cout << "3. Starting the Task.\n"
              << "   This is where the Coroutine calls executor.add().\n";
    auto sf = std::move(te).start();

    std::cout << "4. Stepping the executor.\n"
              << "   The Coroutine runs until it is blocked.\n"
              << "   The call to baz() does not block, so is executed.\n"
              << "   The Baton isn't ready, so execution stops there.\n";
    executor->step();

    std::cout << "5. Posting the Baton.\n"
              << "   This will unblock the Coroutine; it will automatically"
                 " re-add itself to the executor.\n";
    baton.post();

    std::cout << "6. Stepping the executor.\n"
              << "   The Coroutine will now complete its execution.\n";
    executor->step();
  }
  std::cout << "\n";
  {
    std::cout << "1. Making a SemiFuture.\n";
    auto sf1 = folly::makeSemiFuture(111);

    std::cout << "2. Adding 'defered' work to the SemiFuture.\n"
              << "   This work will be run as part of the initial execution.\n";
    auto sf2 = std::move(sf1).deferValue(bar).deferValue(bar);

    std::cout << "3. Assigning an Executor.\n"
              << "   The now-Future will immediately call executor.add().\n";
    auto f1 = std::move(sf2).via(executor);

    std::cout << "4. Adding a continuation.\n"
              << "   Since the first part of the Future hasn't run yet,"
                 " this isn't ready to run, hence is not added.\n";
    auto f2 = std::move(f1).thenValue(bar);

    std::cout << "5. Stepping the executor.\n"
              << "   This will run the defered computation.\n"
              << "   Furthermore, this unblocks the second part of the Future"
                 " (the .thenValue() continuation), so more work will be added"
                 " to the executor.\n";
    executor->step();

    std::cout << "6. Stepping the executor.\n"
              << "   This will run the .thenValue(bar) continuation.\n";
    executor->step();

    std::cout << "7. Adding another continuation.\n"
              << "   Since the Future is ready, this adds immediately.\n";
    auto f3 = std::move(f2).thenValue(bar);

    std::cout << "8. Stepping the executor.\n";
    executor->step();
  }

  return 0;
}
