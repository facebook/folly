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

#include <string>
#include <vector>

#include <folly/debugging/symbolizer/StackTrace.h>
#include <folly/debugging/symbolizer/Symbolizer.h>
#include <folly/debugging/symbolizer/test/OmitFramePointerFunc.h>
#include <folly/lang/Hint.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

using namespace folly;
using namespace folly::symbolizer;

namespace {

// Stack trace captured by the innermost function.
FrameArray<100> gFrames; // NOLINT(facebook-avoid-non-const-global-variables)

// C: the innermost function, compiled with frame pointers (default).
// Captures the stack trace.
FOLLY_NOINLINE void funcC() {
  getStackTraceSafe(gFrames);
}

// A: the outermost function, compiled with frame pointers (default).
// Calls into B (omitFpMiddle) which is compiled with -fomit-frame-pointer.
FOLLY_NOINLINE void funcA() {
  folly::symbolizer::test::omitFpMiddle(&funcC);
  compiler_must_not_elide(0); // prevent tail-call optimization
}

} // namespace

// Verifies that the DWARF-based unwinder (libunwind) correctly unwinds
// through a function compiled with -fomit-frame-pointer.
//
// Call chain: funcA -> omitFpMiddle -> funcC
//   - funcA and funcC: compiled with frame pointers (default)
//   - omitFpMiddle: compiled with -fomit-frame-pointer (separate TU)
//
// The frame pointer chain is broken at omitFpMiddle, but libunwind uses
// DWARF Call Frame Information (.eh_frame), not frame pointers, so it
// should still produce a complete stack trace.
TEST(StackTraceTest, OmitFramePointerUnwind) {
  SKIP_IF(!Symbolizer::isAvailable());

  funcA();

  CHECK_GT(gFrames.frameCount, 0);

  Symbolizer symbolizer;
  symbolizer.symbolize(gFrames);
  StringSymbolizePrinter printer;
  printer.println(gFrames);
  auto stackTraceStr = printer.str();

  LOG(INFO) << "Stack trace with -fomit-frame-pointer middle frame:\n"
            << stackTraceStr;

  // All three functions should appear in the stack trace in this order:
  // funcC (innermost) -> omitFpMiddle (no frame pointer) -> funcA (outermost)
  std::vector<std::string> funcNames{
      "funcC",
      "omitFpMiddle",
      "funcA",
  };
  std::vector<size_t> positions;
  for (const auto& funcName : funcNames) {
    SCOPED_TRACE(funcName);
    auto pos = stackTraceStr.find(funcName);
    ASSERT_NE(pos, std::string::npos)
        << "Function " << funcName << " not found in stack trace";
    positions.push_back(pos);
  }
  // Verify the functions appear in the expected order.
  for (size_t i = 0; i < positions.size() - 1; ++i) {
    ASSERT_LT(positions[i], positions[i + 1])
        << funcNames[i] << " should appear before " << funcNames[i + 1];
  }
}

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
