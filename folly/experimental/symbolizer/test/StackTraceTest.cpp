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

#include <cstring>

#include <folly/experimental/TestUtil.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/symbolizer/StackTrace.h>
#include <folly/experimental/symbolizer/Symbolizer.h>
#include <folly/lang/Hint.h>
#include <folly/test/TestUtils.h>

#include <boost/regex.hpp>

#include <glog/logging.h>

#include <folly/portability/GTest.h>

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
    bytes_read = read(fd, pos, size);
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
  printer.flush();

  std::array<uint8_t, 4000> first;
  off_t pos = get_stack_trace(fd, 0, first.data(), first.size());
  ASSERT_GT(pos, 0);

  printer.printStackTrace(true);
  printer.flush();

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
  // ./folly/experimental/symbolizer/test/StackTraceTest.cpp:202
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
