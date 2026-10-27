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

#include <folly/ThreadLocal.h>

#include <atomic>
#include <cstdlib>
#include <thread>

#include <folly/portability/GTest.h>

namespace {

// Objects with static storage duration are destroyed before the handler below
// runs, so everything it touches is either trivially destructible or leaked.
std::atomic<bool> armed{false};
std::atomic<bool> released{false};
std::thread* background = nullptr;
folly::ThreadLocal<int>* value = nullptr;

void afterStaticDestructors() {
  if (armed.load()) {
    released.store(true);
    background->join();
  }
}

// Runs before the sanitizer runtimes have initialized, so it must not be
// instrumented at all.
__attribute__((disable_sanitizer_instrumentation)) void
registerAfterStaticDestructors() {
  std::atexit(&afterStaticDestructors);
}

// Registered before any static constructor runs, in this executable or in the
// shared libraries it depends on, so the handler runs after every static
// destructor.
[[gnu::section(".preinit_array"),
  gnu::used]] void (*const preinit)() = &registerAfterStaticDestructors;

} // namespace

// A thread that first uses a ThreadLocal while the process is exiting, after
// folly's static destructors have run, must still be able to set its
// thread-specific data.
TEST(ThreadLocal, FirstUseAfterStaticDestructors) {
  EXPECT_EXIT(
      {
        background = new std::thread([] {
          while (!released.load()) {
            std::this_thread::yield();
          }
          value = new folly::ThreadLocal<int>();
          **value = 1;
        });
        armed.store(true);
        std::exit(0);
      },
      testing::ExitedWithCode(0),
      "");
}
