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

#include <folly/debugging/symbolizer/StackTrace.h>
#include <folly/lang/Hint.h>
#include <folly/portability/GTest.h>

#include <atomic>
#include <csignal>
#include <thread>
#include <vector>

#include <folly/portability/Config.h>

#include <execinfo.h>
#include <services_efficiency/backtrace/mapping_range.h>

namespace folly::symbolizer {

class FramePointerUnwinderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!have_proc_map_query()) {
      GTEST_SKIP() << "PROCMAP_QUERY not available (kernel < 6.11)";
    }
  }

  bool isProcMapQueryAvailable() { return have_proc_map_query(); }
};

// =============================================================================
// Basic Functionality Tests - getStackTraceSafe()
// =============================================================================

TEST_F(FramePointerUnwinderTest, BasicUnwindSafe) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available (kernel < 6.11)";
  }

  uintptr_t addresses[100];
  ssize_t count = getStackTraceSafe(addresses, 100);

  EXPECT_GT(count, 0) << "Should capture at least one frame";
  EXPECT_LE(count, 100) << "Should not exceed buffer size";
}

FOLLY_NOINLINE void nestedFunction3Safe(uintptr_t* addrs, ssize_t* count) {
  *count = getStackTraceSafe(addrs, 100);
}

FOLLY_NOINLINE void nestedFunction2Safe(uintptr_t* addrs, ssize_t* count) {
  nestedFunction3Safe(addrs, count);
}

FOLLY_NOINLINE void nestedFunction1Safe(uintptr_t* addrs, ssize_t* count) {
  nestedFunction2Safe(addrs, count);
}

TEST_F(FramePointerUnwinderTest, NestedFunctionsSafe) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  uintptr_t addresses[100];
  ssize_t count = 0;
  nestedFunction1Safe(addresses, &count);

  // Should capture at least: nestedFunction3, nestedFunction2,
  // nestedFunction1, this test, test framework, main
  EXPECT_GE(count, 5) << "Should capture multiple nested frames";
}

FOLLY_NOINLINE void captureTraceSafe(uintptr_t* addrs, ssize_t* count) {
  *count = getStackTraceSafe(addrs, 100);
}

TEST_F(FramePointerUnwinderTest, ConsistentResultsSafe) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  uintptr_t addresses1[100];
  uintptr_t addresses2[100];
  ssize_t count1 = 0;
  ssize_t count2 = 0;

  captureTraceSafe(addresses1, &count1);
  captureTraceSafe(addresses2, &count2);

  // Both calls should succeed
  ASSERT_GT(count1, 0);
  ASSERT_GT(count2, 0);

  // Frame counts should match or be very close
  EXPECT_NEAR(count1, count2, 1);

  // The two calls originate from different call sites within this test body,
  // so the bottom frames (unwinder internals, return addresses into
  // captureTraceSafe) will differ. The number of differing frames varies by
  // build mode (sanitizer instrumentation, inlining, etc.). Verify that the
  // traces share a significant number of common frames using set intersection.
  ssize_t minCount = std::min(count1, count2);
  int shared = 0;
  for (ssize_t i = 0; i < count1; ++i) {
    for (ssize_t j = 0; j < count2; ++j) {
      if (addresses1[i] == addresses2[j]) {
        ++shared;
        break;
      }
    }
  }

  // The majority of frames should be shared (everything above the call sites)
  EXPECT_GE(shared, minCount / 2)
      << "Traces should share at least half their frames. "
      << "count1=" << count1 << ", count2=" << count2 << ", shared=" << shared;
}

// =============================================================================
// Basic Functionality Tests - getStackTrace()
// =============================================================================

TEST_F(FramePointerUnwinderTest, BasicUnwindFast) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available (kernel < 6.11)";
  }

  uintptr_t addresses[100];
  ssize_t count = getStackTrace(addresses, 100);

  EXPECT_GT(count, 0) << "Should capture at least one frame";
  EXPECT_LE(count, 100) << "Should not exceed buffer size";
}

FOLLY_NOINLINE void nestedFunction3Fast(uintptr_t* addrs, ssize_t* count) {
  *count = getStackTrace(addrs, 100);
}

FOLLY_NOINLINE void nestedFunction2Fast(uintptr_t* addrs, ssize_t* count) {
  nestedFunction3Fast(addrs, count);
}

FOLLY_NOINLINE void nestedFunction1Fast(uintptr_t* addrs, ssize_t* count) {
  nestedFunction2Fast(addrs, count);
}

TEST_F(FramePointerUnwinderTest, NestedFunctionsFast) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  uintptr_t addresses[100];
  ssize_t count = 0;
  nestedFunction1Fast(addresses, &count);

  EXPECT_GE(count, 5) << "Should capture multiple nested frames";
}

TEST_F(FramePointerUnwinderTest, GetStackTraceConsistentWithSafe) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  uintptr_t addressesFast[100];
  uintptr_t addressesSafe[100];

  ssize_t countFast = getStackTrace(addressesFast, 100);
  ssize_t countSafe = getStackTraceSafe(addressesSafe, 100);

  // Both should succeed
  EXPECT_GT(countFast, 0);
  EXPECT_GT(countSafe, 0);

  // Frame counts may differ slightly due to call overhead, but should be close
  EXPECT_NEAR(countFast, countSafe, 3);
}

// =============================================================================
// Basic Functionality Tests - getStackTraceHeap()
// =============================================================================

TEST_F(FramePointerUnwinderTest, BasicUnwindHeap) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available (kernel < 6.11)";
  }

  uintptr_t addresses[100];
  ssize_t count = getStackTraceHeap(addresses, 100);

  EXPECT_GT(count, 0) << "Should capture at least one frame";
  EXPECT_LE(count, 100) << "Should not exceed buffer size";
}

FOLLY_NOINLINE void nestedFunction3Heap(uintptr_t* addrs, ssize_t* count) {
  *count = getStackTraceHeap(addrs, 100);
}

FOLLY_NOINLINE void nestedFunction2Heap(uintptr_t* addrs, ssize_t* count) {
  nestedFunction3Heap(addrs, count);
}

FOLLY_NOINLINE void nestedFunction1Heap(uintptr_t* addrs, ssize_t* count) {
  nestedFunction2Heap(addrs, count);
}

TEST_F(FramePointerUnwinderTest, NestedFunctionsHeap) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  uintptr_t addresses[100];
  ssize_t count = 0;
  nestedFunction1Heap(addresses, &count);

  EXPECT_GE(count, 5) << "Should capture multiple nested frames";
}

TEST_F(FramePointerUnwinderTest, GetStackTraceHeapConsistentWithSafe) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  uintptr_t addressesHeap[100];
  uintptr_t addressesSafe[100];

  ssize_t countHeap = getStackTraceHeap(addressesHeap, 100);
  ssize_t countSafe = getStackTraceSafe(addressesSafe, 100);

  // Both should succeed
  EXPECT_GT(countHeap, 0);
  EXPECT_GT(countSafe, 0);

  // Frame counts may differ slightly due to call overhead, but should be close
  EXPECT_NEAR(countHeap, countSafe, 3);
}

// =============================================================================
// Signal Handler Tests
// =============================================================================

namespace {
std::atomic<ssize_t> gSignalTraceCount{-1};
std::array<uintptr_t, 100> gSignalAddresses;

void signalHandler(int) {
  gSignalTraceCount.store(
      getStackTraceSafe(gSignalAddresses.data(), gSignalAddresses.size()),
      std::memory_order_release);
}
} // namespace

TEST_F(FramePointerUnwinderTest, SignalHandlerContext) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  struct sigaction sa{};
  sa.sa_handler = signalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  struct sigaction oldSa{};
  ASSERT_EQ(sigaction(SIGUSR1, &sa, &oldSa), 0);

  gSignalTraceCount.store(-1, std::memory_order_release);
  raise(SIGUSR1);

  ssize_t count = gSignalTraceCount.load(std::memory_order_acquire);
  EXPECT_GT(count, 0) << "Should capture frames from signal handler";

  // Restore old handler
  sigaction(SIGUSR1, &oldSa, nullptr);
}

// =============================================================================
// Multi-threaded Tests
// =============================================================================

TEST_F(FramePointerUnwinderTest, MultiThreadedUnwind) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  constexpr int kNumThreads = 10;
  constexpr int kIterationsPerThread = 1000;

  std::vector<std::thread> threads;
  std::atomic<int> failures{0};
  std::atomic<int> totalFrames{0};

  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&failures, &totalFrames]() {
      uintptr_t addresses[100];
      for (int j = 0; j < kIterationsPerThread; ++j) {
        ssize_t count = getStackTraceSafe(addresses, 100);
        if (count <= 0) {
          ++failures;
        } else {
          totalFrames += static_cast<int>(count);
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(failures.load(), 0) << "All unwinds should succeed";
  EXPECT_GT(totalFrames.load(), 0) << "Should have captured frames";
}

TEST_F(FramePointerUnwinderTest, MultiThreadedUnwindAllFunctions) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  constexpr int kNumThreads = 10;
  constexpr int kIterationsPerThread = 100;

  std::vector<std::thread> threads;
  std::atomic<int> failures{0};

  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&failures, i]() {
      uintptr_t addresses[100];
      for (int j = 0; j < kIterationsPerThread; ++j) {
        ssize_t count = 0;
        // Alternate between the three functions
        switch ((i + j) % 3) {
          case 0:
            count = getStackTrace(addresses, 100);
            break;
          case 1:
            count = getStackTraceSafe(addresses, 100);
            break;
          case 2:
            count = getStackTraceHeap(addresses, 100);
            break;
          default:
            break;
        }
        if (count <= 0) {
          ++failures;
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(failures.load(), 0) << "All unwinds should succeed";
}

// =============================================================================
// Fallback Behavior Tests
// =============================================================================

TEST_F(FramePointerUnwinderTest, FallbackToLibunwind) {
  // This test verifies that getStackTraceSafe works regardless of
  // whether the frame-pointer unwinder is available
  uintptr_t addresses[100];
  ssize_t count = getStackTraceSafe(addresses, 100);

  // Should always succeed (either via FP unwinder or libunwind)
  EXPECT_GT(count, 0) << "Fallback should provide valid trace";
}

TEST_F(FramePointerUnwinderTest, FallbackToUnwBacktrace) {
  // This test verifies that getStackTrace works regardless of
  // whether the frame-pointer unwinder is available
  uintptr_t addresses[100];
  ssize_t count = getStackTrace(addresses, 100);

  // Should always succeed (either via FP unwinder or unw_backtrace)
  EXPECT_GT(count, 0) << "Fallback should provide valid trace";
}

TEST_F(FramePointerUnwinderTest, FallbackHeap) {
  // This test verifies that getStackTraceHeap works regardless of
  // whether the frame-pointer unwinder is available
  uintptr_t addresses[100];
  ssize_t count = getStackTraceHeap(addresses, 100);

  // Should always succeed (either via FP unwinder or heap-allocated libunwind)
  EXPECT_GT(count, 0) << "Fallback should provide valid trace";
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(FramePointerUnwinderTest, ZeroMaxAddresses) {
  uintptr_t addresses[1];
  ssize_t countSafe = getStackTraceSafe(addresses, 0);
  ssize_t countFast = getStackTrace(addresses, 0);
  ssize_t countHeap = getStackTraceHeap(addresses, 0);

  EXPECT_EQ(countSafe, 0)
      << "getStackTraceSafe should return 0 for zero-size buffer";
  EXPECT_EQ(countFast, 0)
      << "getStackTrace should return 0 for zero-size buffer";
  EXPECT_EQ(countHeap, 0)
      << "getStackTraceHeap should return 0 for zero-size buffer";
}

TEST_F(FramePointerUnwinderTest, SmallBuffer) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  uintptr_t addresses[3];
  ssize_t count = getStackTraceSafe(addresses, 3);

  EXPECT_EQ(count, 3) << "Should fill exactly 3 frames";
}

// =============================================================================
// Deep Stack Test
// =============================================================================

namespace {
FOLLY_NOINLINE ssize_t
deepRecursion(uintptr_t* addresses, size_t maxAddresses, int depth) {
  if (depth <= 0) {
    return getStackTraceSafe(addresses, maxAddresses);
  }
  // Prevent tail-call optimization
  ssize_t result = deepRecursion(addresses, maxAddresses, depth - 1);
  folly::compiler_must_not_elide(result);
  return result;
}
} // namespace

TEST_F(FramePointerUnwinderTest, DeepStack) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  constexpr int kTargetDepth = 200;
  uintptr_t addresses[256];

  ssize_t count = deepRecursion(addresses, 256, kTargetDepth);

  // Should capture many frames without crashing
  EXPECT_GT(count, 100) << "Should capture deep stack frames";
  EXPECT_LE(count, 256) << "Should respect buffer limit";

  // Verify frames are valid (non-zero addresses)
  for (ssize_t i = 0; i < count; ++i) {
    EXPECT_NE(addresses[i], 0u) << "Frame " << i << " should be valid";
  }
}

// =============================================================================
// Folly Integration Tests - Frame Pointer Unwinder vs Libunwind
// =============================================================================

FOLLY_NOINLINE void captureComparison(
    uintptr_t* fpAddrs,
    ssize_t* fpCount,
    void** libunwindAddrs,
    int* libunwindCount) {
  // Capture with the fp unwinder via getStackTraceSafe
  *fpCount = getStackTraceSafe(fpAddrs, 100);
  // Capture with libunwind (execinfo backtrace)
  *libunwindCount = backtrace(libunwindAddrs, 100);
}

FOLLY_NOINLINE void nestedForComparison2(
    uintptr_t* fpAddrs,
    ssize_t* fpCount,
    void** libunwindAddrs,
    int* libunwindCount) {
  captureComparison(fpAddrs, fpCount, libunwindAddrs, libunwindCount);
}

FOLLY_NOINLINE void nestedForComparison1(
    uintptr_t* fpAddrs,
    ssize_t* fpCount,
    void** libunwindAddrs,
    int* libunwindCount) {
  nestedForComparison2(fpAddrs, fpCount, libunwindAddrs, libunwindCount);
}

TEST_F(FramePointerUnwinderTest, CompareWithLibunwind) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  uintptr_t fpAddrs[100];
  ssize_t fpCount = 0;
  void* libunwindAddrs[100];
  int libunwindCount = 0;

  nestedForComparison1(fpAddrs, &fpCount, libunwindAddrs, &libunwindCount);

  // Both should succeed
  ASSERT_GT(fpCount, 3) << "FP unwinder should capture multiple frames";
  ASSERT_GT(libunwindCount, 3) << "Libunwind should capture multiple frames";

  // Find overlapping frames. The fp unwinder and libunwind may have different
  // numbers of frames at the top (due to the capture functions themselves)
  // and bottom (libunwind can unwind through more frames). But the middle
  // frames should overlap significantly.
  //
  // Allow ±1 tolerance when comparing addresses: the FP unwinder returns raw
  // return addresses (pointing to the instruction after the call), while
  // backtrace() may adjust IPs by -1 (pointing to the call instruction itself)
  // depending on the unwinder implementation backing it (e.g., libunwind's
  // _Unwind_Backtrace subtracts 1 for non-signal frames).
  int matches = 0;
  for (ssize_t i = 0; i < fpCount; ++i) {
    for (int j = 0; j < libunwindCount; ++j) {
      auto fpAddr = fpAddrs[i];
      auto luAddr = reinterpret_cast<uintptr_t>(libunwindAddrs[j]);
      if (fpAddr == luAddr || fpAddr == luAddr + 1 || fpAddr + 1 == luAddr) {
        ++matches;
        break;
      }
    }
  }

  // At least a few frames should match between the two unwinders
  EXPECT_GE(matches, 3)
      << "FP unwinder and libunwind should agree on at least 3 frames. "
      << "FP count=" << fpCount << ", libunwind count=" << libunwindCount
      << ", matches=" << matches;
}

TEST_F(FramePointerUnwinderTest, FramePointersAreMonotonic) {
  if (!isProcMapQueryAvailable()) {
    GTEST_SKIP() << "PROCMAP_QUERY not available";
  }

  // Verify that calling getStackTraceSafe from different stack depths produces
  // consistent, valid results - which would fail if monotonicity checking
  // broke valid frame chains.

  // Capture at two different stack depths
  uintptr_t shallow[100];
  ssize_t shallowCount = getStackTraceSafe(shallow, 100);

  uintptr_t deep[100];
  ssize_t deepCount = deepRecursion(deep, 100, 10);

  ASSERT_GT(shallowCount, 0);
  ASSERT_GT(deepCount, 0);
  EXPECT_GT(deepCount, shallowCount)
      << "Deeper stack should produce more frames";

  // Verify all captured addresses are non-zero
  for (ssize_t i = 0; i < shallowCount; ++i) {
    EXPECT_NE(shallow[i], 0u) << "Frame " << i << " should be non-zero";
  }
}

} // namespace folly::symbolizer
