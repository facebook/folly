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

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

#include <folly/ScopeGuard.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/debugging/symbolizer/StackTrace.h>
#include <folly/debugging/symbolizer/Symbolizer.h>
#include <folly/lang/Hint.h>
#include <folly/portability/SysMman.h>
#include <folly/test/TestUtils.h>
#include <folly/testing/TestUtil.h>
#include <folly/tracing/AsyncStack.h>
#include <folly/tracing/test/AsyncStackTestUtils.h>

#include <boost/regex.hpp>

#include <glog/logging.h>

#include <folly/portability/GTest.h>

namespace {

using folly::test::MappedPage;

#if FOLLY_HAS_ASYNC_STACK_METADATA

using folly::AsyncStackFrame;
using folly::test::linkInactiveFrames;
using folly::test::markAsMetadataFrame;
using folly::test::ScopedAsyncStackRootWithFrame;

struct CapturedAsyncStack {
  std::array<std::uintptr_t, 100> addresses{};
  // Includes the native frames leading to the test's async root.
  ssize_t frameCount{};
};

// Keep this out of line so its stack frame remains visible to the walker.
FOLLY_NOINLINE ssize_t
captureCurrentAsyncStack(std::uintptr_t* addresses, size_t maxAddresses) {
  auto count =
      folly::symbolizer::getAsyncStackTraceSafe(addresses, maxAddresses);
  folly::compiler_must_not_elide(count);
  return count;
}

CapturedAsyncStack captureCurrentAsyncStack() {
  CapturedAsyncStack capture;
  capture.frameCount = captureCurrentAsyncStack(
      capture.addresses.data(), capture.addresses.size());
  return capture;
}

size_t countCapturedAddress(
    const CapturedAsyncStack& capture, const void* address) {
  size_t count = 0;
  for (size_t i = 0; i < static_cast<size_t>(capture.frameCount); ++i) {
    count += capture.addresses[i] == reinterpret_cast<std::uintptr_t>(address);
  }
  return count;
}

void expectNoMetadataMarkers(const CapturedAsyncStack& capture) {
  for (size_t i = 0; i < static_cast<size_t>(capture.frameCount); ++i) {
    EXPECT_NE(capture.addresses[i], folly_async_stack_metadata_frame_cookie);
  }
}

#endif

// Keep this out of line so its stack frame remains visible to the walker.
FOLLY_NOINLINE ssize_t captureWithUnreadableCurrentRoot(
    folly::AsyncStackRoot* unreadableRoot,
    std::uintptr_t* addresses,
    size_t maxAddresses) {
  auto* previousRoot = folly::exchangeCurrentAsyncStackRoot(unreadableRoot);
  auto count =
      folly::symbolizer::getAsyncStackTraceSafe(addresses, maxAddresses);
  CHECK_EQ(folly::exchangeCurrentAsyncStackRoot(previousRoot), unreadableRoot);
  return count;
}

} // namespace

TEST(StackTraceTest, ZeroCapacityDoesNotReadCurrentRoot) {
  // Crash-time state can be unreadable. Zero capacity must return before
  // touching the current root.
  MappedPage<folly::AsyncStackRoot> unreadableRoot{PROT_NONE};
  EXPECT_EQ(
      captureWithUnreadableCurrentRoot(unreadableRoot.get(), nullptr, 0), 0);
}

TEST(StackTraceTest, CurrentAddressAtCapacityDoesNotReadCurrentRoot) {
  // The current return address fills the only output slot. The walker must not
  // then dereference the unreadable current async root.
  MappedPage<folly::AsyncStackRoot> unreadableRoot{PROT_NONE};
  constexpr auto kCanary = std::numeric_limits<std::uintptr_t>::max();
  std::uintptr_t address = kCanary;
  EXPECT_EQ(
      captureWithUnreadableCurrentRoot(unreadableRoot.get(), &address, 1), 1);
  EXPECT_NE(address, kCanary);
}

#if FOLLY_HAS_BUILTIN(__builtin_frame_address)

namespace {

// NOLINTNEXTLINE(facebook-avoid-non-const-global-variables): API takes void*
char capacityAddressSentinel;

// Keep this out of line so its stack frame remains visible to the walker.
FOLLY_NOINLINE ssize_t captureAsyncStackAtCapacity(
    folly::AsyncStackRoot* unreadableRoot,
    std::array<std::uintptr_t, 2>& addresses) {
  using namespace folly;

  auto* previousRoot = tryGetCurrentAsyncStackRoot();

  detail::ScopedAsyncStackRoot scopedRoot;
  AsyncStackFrame frame;
  frame.setReturnAddress(&capacityAddressSentinel);
  scopedRoot.activateFrame(frame);

  auto& root = getCurrentAsyncStackRoot();
  root.setNextRoot(unreadableRoot);
  // `ScopedAsyncStackRoot` restores TLS from `nextRoot` in its destructor.
  // Restore that link and detach its frame before the destructor runs.
  SCOPE_EXIT {
    root.setNextRoot(previousRoot);
    deactivateAsyncStackFrame(frame);
  };
  return symbolizer::getAsyncStackTraceSafe(addresses.data(), addresses.size());
}

} // namespace

TEST(StackTraceTest, AsyncFrameAtCapacityDoesNotReadNextRoot) {
  // The async frame fills the second output slot. The walker must not then
  // follow it into the unreadable next root.
  MappedPage<folly::AsyncStackRoot> unreadableRoot{PROT_NONE};
  std::array<std::uintptr_t, 2> addresses{};
  EXPECT_EQ(captureAsyncStackAtCapacity(unreadableRoot.get(), addresses), 2);
  EXPECT_EQ(
      addresses[1], reinterpret_cast<std::uintptr_t>(&capacityAddressSentinel));
}
#endif

#if FOLLY_HAS_ASYNC_STACK_METADATA

TEST(StackTraceTest, AsyncStackTraceOmitsMetadataMarkers) {
  // Parent links in the hand-built chains:
  //   baseline: root -> wrapper -> parent
  //   marked:   root -> [marker] -> wrapper -> [marker] -> parent
  // The API does not start a root with a marker; that extra marker exercises
  // the defensive entry path. Neither marker is a code frame.
  AsyncStackFrame parent;
  AsyncStackFrame wrapper;
  parent.setReturnAddress(&parent);
  wrapper.setReturnAddress(&wrapper);
  linkInactiveFrames(wrapper, parent);

  CapturedAsyncStack baseline;
  {
    ScopedAsyncStackRootWithFrame activeRoot{wrapper};
    baseline = captureCurrentAsyncStack();
  }

  AsyncStackFrame parentMarker;
  markAsMetadataFrame(parentMarker);
  linkInactiveFrames(parentMarker, parent);
  AsyncStackFrame initialMarker;
  markAsMetadataFrame(initialMarker);
  linkInactiveFrames(initialMarker, wrapper);
  wrapper.setParentFrame(parentMarker);
  CapturedAsyncStack marked;
  {
    ScopedAsyncStackRootWithFrame activeRoot{initialMarker};
    marked = captureCurrentAsyncStack();
  }

  ASSERT_GE(baseline.frameCount, 2);
  ASSERT_EQ(marked.frameCount, baseline.frameCount);
  EXPECT_EQ(countCapturedAddress(baseline, &wrapper), 1);
  EXPECT_EQ(countCapturedAddress(baseline, &parent), 1);
  EXPECT_EQ(countCapturedAddress(marked, &wrapper), 1);
  EXPECT_EQ(countCapturedAddress(marked, &parent), 1);
  expectNoMetadataMarkers(marked);
}

TEST(StackTraceTest, AsyncStackTraceStopsBeforeParentAfterAdjacentMarkers) {
  // Parent links: root -> wrapper -> [marker] -> [marker] -> parent.
  // The API cannot create adjacent markers. The walker stops at any adjacent
  // pair because a marker cycle would neither fill the output nor terminate.
  AsyncStackFrame parent;
  AsyncStackFrame outerMarker;
  AsyncStackFrame innerMarker;
  AsyncStackFrame wrapper;
  parent.setReturnAddress(&parent);
  markAsMetadataFrame(outerMarker);
  markAsMetadataFrame(innerMarker);
  wrapper.setReturnAddress(&wrapper);
  linkInactiveFrames(wrapper, innerMarker, outerMarker, parent);
  CapturedAsyncStack capture;
  {
    ScopedAsyncStackRootWithFrame activeRoot{wrapper};
    capture = captureCurrentAsyncStack();
  }

  ASSERT_GE(capture.frameCount, 1);
  EXPECT_EQ(countCapturedAddress(capture, &wrapper), 1);
  EXPECT_EQ(countCapturedAddress(capture, &parent), 0);
  expectNoMetadataMarkers(capture);
}

TEST(StackTraceTest, AsyncStackTraceStopsOnAdjacentInitialMarkers) {
  // Parent links: root -> [marker] -> [marker] -> wrapper.
  // The API cannot start a root with adjacent markers. The walker stops at any
  // adjacent pair because a marker cycle would never terminate.
  AsyncStackFrame outerMarker;
  AsyncStackFrame innerMarker;
  AsyncStackFrame wrapper;
  markAsMetadataFrame(outerMarker);
  markAsMetadataFrame(innerMarker);
  wrapper.setReturnAddress(&wrapper);
  linkInactiveFrames(innerMarker, outerMarker, wrapper);
  CapturedAsyncStack capture;
  {
    ScopedAsyncStackRootWithFrame activeRoot{innerMarker};
    capture = captureCurrentAsyncStack();
  }

  ASSERT_GE(capture.frameCount, 1);
  EXPECT_EQ(countCapturedAddress(capture, &wrapper), 0);
  expectNoMetadataMarkers(capture);
}

#if FOLLY_HAS_BUILTIN(__builtin_frame_address)

TEST(StackTraceTest, AsyncStackTraceDoesNotReadInitialFrameAtCapacity) {
  // Locate the async frame in a normal trace, then protect it and stop output
  // immediately before it. Any attempted read of that frame faults.
  MappedPage<AsyncStackFrame> frameMemory{PROT_READ | PROT_WRITE};
  auto* frame = ::new (frameMemory.get()) AsyncStackFrame;
  SCOPE_EXIT {
    frame->~AsyncStackFrame();
  };
  frame->setReturnAddress(&capacityAddressSentinel);

  {
    ScopedAsyncStackRootWithFrame activeRoot{*frame};

    std::array<std::uintptr_t, 100> fullTrace{};
    const auto fullCount =
        captureCurrentAsyncStack(fullTrace.data(), fullTrace.size());
    const auto fullSize = static_cast<size_t>(fullCount);
    size_t asyncFrameIndex = fullSize;
    for (size_t i = 0; i < fullSize; ++i) {
      if (fullTrace[i] ==
          reinterpret_cast<std::uintptr_t>(&capacityAddressSentinel)) {
        asyncFrameIndex = i;
        break;
      }
    }
    ASSERT_LT(asyncFrameIndex, fullSize);
    ASSERT_GT(asyncFrameIndex, 1);

    frameMemory.protectOrFatal(PROT_NONE);
    SCOPE_EXIT {
      frameMemory.protectOrFatal(PROT_READ | PROT_WRITE);
    };

    std::array<std::uintptr_t, 100> limitedTrace{};
    const auto limitedCount =
        captureCurrentAsyncStack(limitedTrace.data(), asyncFrameIndex);

    EXPECT_EQ(limitedCount, static_cast<ssize_t>(asyncFrameIndex));
  }
}

namespace {

struct CapturedAsyncStackPair {
  CapturedAsyncStack baseline;
  CapturedAsyncStack marked;
};

// NOLINTNEXTLINE(facebook-avoid-non-const-global-variables): API takes void*
char wrapperAddressSentinel;
// NOLINTNEXTLINE(facebook-avoid-non-const-global-variables): API takes void*
char outerAddressSentinel;

// Captures one root before and after adding its terminal marker:
//   baseline: root -> wrapper; marked: root -> wrapper -> [marker]
// Keep this out of line so its stack frame remains visible to the walker.
FOLLY_NOINLINE CapturedAsyncStackPair captureInnerAsyncStacks() {
  AsyncStackFrame marker;
  AsyncStackFrame wrapper;
  markAsMetadataFrame(marker);
  wrapper.setReturnAddress(&wrapperAddressSentinel);

  CapturedAsyncStackPair captures;
  {
    ScopedAsyncStackRootWithFrame activeRoot{wrapper};
    captures.baseline = captureCurrentAsyncStack();
  }
  wrapper.setParentFrame(marker);
  {
    ScopedAsyncStackRootWithFrame activeRoot{wrapper};
    captures.marked = captureCurrentAsyncStack();
  }
  return captures;
}

// Adds an outer root around both captures (`=>` crosses the native stack):
//   baseline: inner root -> wrapper => outer root -> outer
//   marked: inner root -> wrapper -> [marker] => outer root -> outer
// Keep this out of line so its stack frame remains visible to the walker.
FOLLY_NOINLINE CapturedAsyncStackPair captureNestedAsyncStacks() {
  AsyncStackFrame outer;
  outer.setReturnAddress(&outerAddressSentinel);

  ScopedAsyncStackRootWithFrame activeRoot{outer};
  return captureInnerAsyncStacks();
}

} // namespace

TEST(StackTraceTest, AsyncStackTraceOmitsMarkerAndContinuesToOuterRoot) {
  const auto captures = captureNestedAsyncStacks();

  ASSERT_GE(captures.baseline.frameCount, 2);
  ASSERT_EQ(captures.marked.frameCount, captures.baseline.frameCount);
  // Both traces enter through the same noinline helper. The active root stops
  // the walk before these call sites, so every visible address should match.
  for (size_t i = 0; i < static_cast<size_t>(captures.marked.frameCount); ++i) {
    EXPECT_EQ(captures.marked.addresses[i], captures.baseline.addresses[i])
        << "frame " << i;
  }
  size_t wrapperIndex = static_cast<size_t>(captures.marked.frameCount);
  size_t outerIndex = static_cast<size_t>(captures.marked.frameCount);
  for (size_t i = 0; i < static_cast<size_t>(captures.marked.frameCount); ++i) {
    if (captures.marked.addresses[i] ==
        reinterpret_cast<std::uintptr_t>(&wrapperAddressSentinel)) {
      wrapperIndex = i;
    }
    if (captures.marked.addresses[i] ==
        reinterpret_cast<std::uintptr_t>(&outerAddressSentinel)) {
      outerIndex = i;
    }
  }
  ASSERT_EQ(countCapturedAddress(captures.marked, &wrapperAddressSentinel), 1);
  ASSERT_EQ(countCapturedAddress(captures.marked, &outerAddressSentinel), 1);
  EXPECT_LT(wrapperIndex, outerIndex);
  expectNoMetadataMarkers(captures.marked);
}

#endif
#endif

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

using namespace folly;
using namespace folly::symbolizer;

FOLLY_NOINLINE void foo1();
FOLLY_NOINLINE void foo2();

void verifyStackTraces() {
  constexpr size_t kMaxAddresses = 100;
  FrameArray<kMaxAddresses> fa;
  CHECK(getStackTrace(fa));

  FrameArray<kMaxAddresses> faSafe;
  CHECK(getStackTraceSafe(faSafe));

  FrameArray<kMaxAddresses> faHeap;
  CHECK(getStackTraceHeap(faHeap));

  CHECK_EQ(fa.frameCount, faSafe.frameCount);
  CHECK_EQ(fa.frameCount, faHeap.frameCount);

  if (VLOG_IS_ON(1)) {
    Symbolizer symbolizer;
    OStreamSymbolizePrinter printer(std::cerr, SymbolizePrinter::COLOR_IF_TTY);

    symbolizer.symbolize(fa);
    VLOG(1) << "getStackTrace\n";
    printer.println(fa);

    symbolizer.symbolize(faSafe);
    VLOG(1) << "getStackTraceSafe\n";
    printer.println(faSafe);

    symbolizer.symbolize(faHeap);
    VLOG(1) << "getStackTraceHeap\n";
    printer.println(faHeap);
  }

  // Other than the top 2 frames (this one and getStackTrace /
  // getStackTraceSafe), the stack traces should be identical
  for (size_t i = 2; i < fa.frameCount; ++i) {
    LOG(INFO) << "i=" << i << " " << std::hex << "0x" << fa.addresses[i]
              << " 0x" << faSafe.addresses[i] << " 0x" << faHeap.addresses[i];
    EXPECT_EQ(fa.addresses[i], faSafe.addresses[i]);
    EXPECT_EQ(fa.addresses[i], faHeap.addresses[i]);
  }
}

void foo1() {
  foo2();
}

void foo2() {
  verifyStackTraces();
}

volatile bool handled = false;
void handler(int /* num */, siginfo_t* /* info */, void* /* ctx */) {
  // Yes, getStackTrace and VLOG aren't async-signal-safe, but signals
  // raised with raise() aren't "async" signals.
  foo1();
  handled = true;
}

TEST(StackTraceTest, Simple) {
  foo1();
}

TEST(StackTraceTest, Signal) {
  if (folly::kIsSanitizeThread) {
    // TSAN doesn't like signal-unsafe functions in a signal handler regardless
    // of how the signal is raised. So skip the test in that case.
    SKIP() << "Not supported for TSAN";
  }
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = handler;
  sa.sa_flags = SA_RESETHAND | SA_SIGINFO;
  CHECK_ERR(sigaction(SIGUSR1, &sa, nullptr));
  raise(SIGUSR1);
  EXPECT_TRUE(handled);
}

ssize_t read_all(int fd, uint8_t* buffer, size_t size) {
  uint8_t* pos = buffer;
  ssize_t bytes_read;
  do {
    bytes_read = fileops::read(fd, pos, size);
    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        continue;
      }
      return bytes_read;
    }

    pos += bytes_read;
    size -= bytes_read;
  } while (bytes_read > 0 && size > 0);

  return pos - buffer;
}

// Returns the position in the file after done reading.
off_t get_stack_trace(int fd, size_t file_pos, uint8_t* buffer, size_t count) {
  off_t rv = lseek(fd, file_pos, SEEK_SET);
  CHECK_EQ(rv, (off_t)file_pos);

  // Subtract 1 from size of buffer to hold nullptr.
  ssize_t bytes_read = read_all(fd, buffer, count - 1);
  CHECK_GT(bytes_read, 0);
  buffer[bytes_read] = '\0';
  return lseek(fd, 0, SEEK_CUR);
}

template <class StackTracePrinter>
void testStackTracePrinter(StackTracePrinter& printer, int fd) {
  ASSERT_GT(fd, 0);

  printer.printStackTrace(true);

  std::array<uint8_t, 4000> first;
  off_t pos = get_stack_trace(fd, 0, first.data(), first.size());
  ASSERT_GT(pos, 0);

  printer.printStackTrace(true);

  std::array<uint8_t, 4000> second;
  get_stack_trace(fd, pos, second.data(), second.size());

  // The first two lines refer to this stack frame, which is different in the
  // two cases, so strip those off.  The rest should be equal.
  ASSERT_STREQ(
      strchr(strchr((const char*)first.data(), '\n') + 1, '\n') + 1,
      strchr(strchr((const char*)second.data(), '\n') + 1, '\n') + 1);
}

TEST(StackTraceTest, SafeStackTracePrinter) {
  test::TemporaryFile file;

  SafeStackTracePrinter printer{file.fd()};

  testStackTracePrinter<SafeStackTracePrinter>(printer, file.fd());
}

TEST(StackTraceTest, FastStackTracePrinter) {
  test::TemporaryFile file;

  FastStackTracePrinter printer{
      std::make_unique<FDSymbolizePrinter>(file.fd())};

  testStackTracePrinter<FastStackTracePrinter>(printer, file.fd());
}

TEST(StackTraceTest, TerseStackTracePrinter) {
  test::TemporaryFile file;

  FastStackTracePrinter printer{
      std::make_unique<FDSymbolizePrinter>(file.fd(), SymbolizePrinter::TERSE)};

  testStackTracePrinter<FastStackTracePrinter>(printer, file.fd());
}

TEST(StackTraceTest, TerseFileAndLineStackTracePrinter) {
  test::TemporaryFile file;

  FastStackTracePrinter printer{std::make_unique<FDSymbolizePrinter>(
      file.fd(), SymbolizePrinter::TERSE_FILE_AND_LINE)};

  testStackTracePrinter<FastStackTracePrinter>(printer, file.fd());
}

TEST(StackTraceTest, TwoStepFastStackTracePrinter) {
  test::TemporaryFile file;

  TwoStepFastStackTracePrinter printer{
      std::make_unique<FDSymbolizePrinter>(file.fd())};

  testStackTracePrinter<TwoStepFastStackTracePrinter>(printer, file.fd());
}

TEST(StackTraceTest, TwoStepTerseStackTracePrinter) {
  test::TemporaryFile file;

  TwoStepFastStackTracePrinter printer{
      std::make_unique<FDSymbolizePrinter>(file.fd(), SymbolizePrinter::TERSE)};

  testStackTracePrinter<TwoStepFastStackTracePrinter>(printer, file.fd());
}

TEST(StackTraceTest, TwoStepTerseFileAndLineStackTracePrinter) {
  test::TemporaryFile file;

  TwoStepFastStackTracePrinter printer{std::make_unique<FDSymbolizePrinter>(
      file.fd(), SymbolizePrinter::TERSE_FILE_AND_LINE)};

  testStackTracePrinter<TwoStepFastStackTracePrinter>(printer, file.fd());
}

namespace {
constexpr int frames = 5;
FOLLY_NOINLINE void foo(FrameArray<frames>& addresses) {
  getStackTraceSafe(addresses);
}

FOLLY_NOINLINE void bar(FrameArray<frames>& addresses) {
  foo(addresses);
}

FOLLY_NOINLINE void baz(FrameArray<frames>& addresses) {
  bar(addresses);
}
} // namespace

TEST(StackTraceTest, TerseFileAndLineStackTracePrinterOutput) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(LocationInfoMode::FULL);
  FrameArray<frames> addresses;
  StringSymbolizePrinter printer(SymbolizePrinter::TERSE_FILE_AND_LINE);
  baz(addresses);
  symbolizer.symbolize(addresses);
  printer.println(addresses, 0);

  // Match a sequence of file+line results that should appear as:
  // ./folly/debugging/symbolizer/test/StackTraceTest.cpp:202
  // or:
  // (unknown)
  boost::regex regex("((([^:]*:[0-9]*)|(\\(unknown\\)))\n)+");
  auto match = boost::regex_match(
      printer.str(), regex, boost::regex_constants::match_not_dot_newline);

  ASSERT_TRUE(match);
}

namespace {
FOLLY_ALWAYS_INLINE void verifyAsyncStackTraces() {
  constexpr size_t kMaxAddresses = 100;
  FrameArray<kMaxAddresses> fa;
  CHECK(getAsyncStackTraceSafe(fa));

  CHECK_GT(fa.frameCount, 0);

  Symbolizer symbolizer;
  symbolizer.symbolize(fa);
  symbolizer::StringSymbolizePrinter printer;
  printer.println(fa);
  auto stackTraceStr = printer.str();

  if (VLOG_IS_ON(1)) {
    VLOG(1) << "getAsyncStackTrace\n" << stackTraceStr;
  }

  // These functions should appear in this relative order
  std::vector<std::string> funcNames{
      "funcF",
      "funcE",
      "co_funcD",
      "co_funcC",
      "funcB2_blocking",
      "funcB1",
      "co_funcB0",
      "co_funcA2",
      "co_funcA1",
      "co_funcA0",
  };
  std::vector<size_t> positions;
  for (const auto& funcName : funcNames) {
    SCOPED_TRACE(funcName);
    auto pos = stackTraceStr.find(funcName);
    ASSERT_NE(pos, std::string::npos);
    positions.push_back(pos);
  }
  for (size_t i = 0; i < positions.size() - 1; ++i) {
    ASSERT_LT(positions[i], positions[i + 1]);
  }
}

FOLLY_NOINLINE void funcF() {
  verifyAsyncStackTraces();
}

FOLLY_NOINLINE void funcE() {
  funcF();
  compiler_must_not_elide(0); // prevent tail-call above
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcD() {
  funcE();
  co_return;
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcC() {
  co_await co_funcD();
}

FOLLY_NOINLINE void funcB2_blocking() {
  // This should trigger a new AsyncStackRoot
  folly::coro::blockingWait(co_funcC());
}

FOLLY_NOINLINE void funcB1() {
  funcB2_blocking();
  compiler_must_not_elide(0); // prevent tail-call above
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcB0() {
  funcB1();
  co_return;
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcA2() {
  co_await co_funcB0();
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcA1() {
  co_await co_funcA2();
}

FOLLY_NOINLINE folly::coro::Task<void> co_funcA0() {
  co_await co_funcA1();
}
} // namespace

TEST(StackTraceTest, AsyncStackTraceSimple) {
  folly::coro::blockingWait(co_funcA0());
}

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
