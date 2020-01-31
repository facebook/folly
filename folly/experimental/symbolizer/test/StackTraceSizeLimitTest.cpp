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

#include <folly/experimental/symbolizer/StackTrace.h>
#include <folly/experimental/symbolizer/Symbolizer.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/SimpleLoopController.h>
#include <folly/init/Init.h>

#include <folly/portability/GTest.h>

using namespace folly::fibers;
using namespace folly::symbolizer;

constexpr size_t kMaxAddresses = 1000;

void fBaseline(FrameArray<kMaxAddresses>& /* unused */) {
  auto ffa = std::make_unique<FrameArray<kMaxAddresses>>();
  (void)ffa;
}

void fStack(FrameArray<kMaxAddresses>& fa) {
  auto ffa = std::make_unique<FrameArray<kMaxAddresses>>();
  (void)ffa;
  getStackTrace(fa);
}

void fHeap(FrameArray<kMaxAddresses>& fa) {
  auto ffa = std::make_unique<FrameArray<kMaxAddresses>>();
  (void)ffa;
  getStackTraceHeap(fa);
}

// Check that the requires stacks for capturing a stack trace do not
// exceed reasonable levels surprisingly.
TEST(StackTraceSizeLimitTest, FiberLimit) {
  FiberManager::Options opts;
  opts.recordStackEvery = 1;

  auto t = [&](folly::Function<void(FrameArray<kMaxAddresses>&)> f,
               size_t highWaterMark) {
    FiberManager manager(std::make_unique<SimpleLoopController>(), opts);
    auto& loopController =
        dynamic_cast<SimpleLoopController&>(manager.loopController());

    bool checkRan = false;
    FrameArray<kMaxAddresses> fa;
    manager.addTask([&] {
      checkRan = true;
      f(fa);
    });

    EXPECT_EQ(manager.stackHighWatermark(), 0);

    loopController.loop([&]() { loopController.stop(); });

    EXPECT_LE(manager.stackHighWatermark(), highWaterMark);

    EXPECT_TRUE(checkRan);
  };
  // Initial run
  t(fBaseline, 10000);

#ifdef NDEBUG
  // Baseline
  t(fBaseline, 1600);
  // Standard version
  t(fStack, 6800);
  // Heap version
  t(fHeap, 2200);
#else
  // Values for opt builds

#ifndef FOLLY_SANITIZE_ADDRESS
  // Baseline
  t(fBaseline, 3700);
  // Standard version
  t(fStack, 8000);
  // Heap version
  t(fHeap, 3700);
#else
  // Baseline
  t(fBaseline, 0);
  // Standard version
  t(fStack, 0);
  // Heap version
  t(fHeap, 0);
#endif
#endif
}
