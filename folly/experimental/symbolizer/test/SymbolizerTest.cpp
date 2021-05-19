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

#include <folly/experimental/symbolizer/Symbolizer.h>

#include <array>
#include <cstdlib>

#include <folly/Demangle.h>
#include <folly/Range.h>
#include <folly/String.h>
#include <folly/experimental/symbolizer/ElfCache.h>
#include <folly/experimental/symbolizer/SymbolizedFrame.h>
#include <folly/experimental/symbolizer/detail/Debug.h>
#include <folly/experimental/symbolizer/test/SymbolizerTestUtils.h>
#include <folly/portability/Filesystem.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

namespace folly {
namespace symbolizer {
namespace test {

void foo() {}

FOLLY_NOINLINE void bar() {
  std::array<int, 2> a = {{1, 2}};
  // Use qsort, which is in a different library
  qsort(a.data(), 2, sizeof(int), testComparator);
}

TEST(Symbolizer, Single) {
  SKIP_IF(!Symbolizer::isAvailable());

  // It looks like we could only use .debug_aranges with "-g2", with
  // "-g1 -gdwarf-aranges", the code has to fallback to line-tables to
  // get the file name.
  Symbolizer symbolizer(LocationInfoMode::FULL);
  SymbolizedFrame a;
  ASSERT_TRUE(symbolizer.symbolize(reinterpret_cast<uintptr_t>(foo), a));
  EXPECT_EQ("folly::symbolizer::test::foo()", folly::demangle(a.name));

  auto path = a.location.file.toString();
  folly::StringPiece basename(path);
  auto pos = basename.rfind('/');
  if (pos != folly::StringPiece::npos) {
    basename.advance(pos + 1);
  }
  EXPECT_EQ("SymbolizerTest.cpp", basename.str());
}

// Test stack frames...

class ElfCacheTest : public testing::Test {
 protected:
  void SetUp() override;
};

// Capture "golden" stack trace with default-configured Symbolizer
FrameArray<100> goldenFrames;

void ElfCacheTest::SetUp() {
  SKIP_IF(!Symbolizer::isAvailable());

  gComparatorGetStackTraceArg = &goldenFrames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  bar();

  Symbolizer symbolizer;
  symbolizer.symbolize(goldenFrames);
  // At least 3 stack frames from us + getStackTrace()
  ASSERT_LE(4, goldenFrames.frameCount);
}

void runElfCacheTest(Symbolizer& symbolizer) {
  FrameArray<100> frames = goldenFrames;
  for (size_t i = 0; i < frames.frameCount; ++i) {
    frames.frames[i].clear();
  }
  symbolizer.symbolize(frames);
  ASSERT_LE(4, frames.frameCount);
  for (size_t i = 1; i < 4; ++i) {
    EXPECT_STREQ(goldenFrames.frames[i].name, frames.frames[i].name);
  }
}

TEST_F(ElfCacheTest, ElfCache) {
  ElfCache cache;
  Symbolizer symbolizer(&cache);
  for (size_t i = 0; i < 2; ++i) {
    runElfCacheTest(symbolizer);
  }
}

TEST_F(ElfCacheTest, SignalSafeElfCache) {
  SignalSafeElfCache cache;
  Symbolizer symbolizer(&cache);
  for (size_t i = 0; i < 2; ++i) {
    runElfCacheTest(symbolizer);
  }
}

TEST(SymbolizerTest, SymbolCache) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL, 100);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  bar();
  symbolizer.symbolize(frames);

  FrameArray<100> frames2;
  gComparatorGetStackTraceArg = &frames2;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  bar();
  symbolizer.symbolize(frames2);
  for (size_t i = 0; i < frames.frameCount; i++) {
    EXPECT_STREQ(frames.frames[i].name, frames2.frames[i].name);
  }
}

namespace {

void expectFrameEq(
    SymbolizedFrame frame,
    const std::string& shortName,
    const std::string& fullName,
    const std::string& file,
    size_t lineno) {
  auto normalizePath = [](std::string path) {
    path = fs::lexically_normal(path).native();
    return (path.find("./", 0) != std::string::npos) ? path.substr(2) : path;
  };
  auto demangled = folly::demangle(frame.name);
  EXPECT_TRUE(demangled == shortName || demangled == fullName)
      << "Found: demangled=" << demangled
      << " expecting shortName=" << shortName << " or fullName=" << fullName;
  EXPECT_EQ(normalizePath(frame.location.file.toString()), normalizePath(file))
      << ' ' << fullName;
  EXPECT_EQ(frame.location.line, lineno) << ' ' << fullName;
}

template <size_t kNumFrames = 100>
void expectFramesEq(
    const FrameArray<kNumFrames>& frames1,
    const FrameArray<kNumFrames>& frames2) {
  EXPECT_EQ(frames1.frameCount, frames2.frameCount);
  for (size_t i = 0; i < frames1.frameCount; i++) {
    EXPECT_STREQ(frames1.frames[i].name, frames2.frames[i].name);
  }
}

} // namespace

TEST(SymbolizerTest, InlineFunctionBasic) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_inlineB_inlineA_qsort();
  symbolizer.symbolize(frames);

  expectFrameEq(
      frames.frames[5],
      "inlineA_qsort",
      "folly::symbolizer::test::inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_qsort);

  expectFrameEq(
      frames.frames[6],
      "inlineB_inlineA_qsort",
      "folly::symbolizer::test::inlineB_inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_inlineA_qsort);

  FrameArray<100> frames2;
  gComparatorGetStackTraceArg = &frames2;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_inlineB_inlineA_qsort();
  symbolizer.symbolize(frames2);

  expectFramesEq(frames, frames2);
}

FOLLY_NOINLINE void call_B_A_qsort() {
  kLineno_inlineB_inlineA_qsort = __LINE__ + 1;
  inlineB_inlineA_qsort();
}

TEST(SymbolizerTest, InlineFunctionWithoutEnoughFrames) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_B_A_qsort();
  symbolizer.symbolize(frames);

  std::array<SymbolizedFrame, 2> limitedFrames;

  // The address of the line where call_B_A_qsort calls inlineB_inlineA_qsort.
  symbolizer.symbolize(
      folly::Range<const uintptr_t*>(&frames.addresses[5], 1),
      folly::range(limitedFrames));

  expectFrameEq(
      limitedFrames[0],
      "inlineB_inlineA_qsort",
      "folly::symbolizer::test::inlineB_inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_inlineA_qsort);
  expectFrameEq(
      limitedFrames[1],
      "call_B_A_qsort",
      "folly::symbolizer::test::call_B_A_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTest.cpp",
      kLineno_inlineB_inlineA_qsort);
}

TEST(SymbolizerTest, InlineFunctionInLexicalBlock) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_lexicalBlock_inlineB_inlineA_qsort();
  symbolizer.symbolize(frames);

  expectFrameEq(
      frames.frames[5],
      "inlineA_qsort",
      "folly::symbolizer::test::inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_qsort);

  expectFrameEq(
      frames.frames[6],
      "inlineB_inlineA_qsort",
      "folly::symbolizer::test::inlineB_inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_inlineA_qsort);

  expectFrameEq(
      frames.frames[7],
      "lexicalBlock_inlineB_inlineA_qsort",
      "folly::symbolizer::test::lexicalBlock_inlineB_inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils.cpp",
      kLineno_inlineB_inlineA_qsort);
}

TEST(SymbolizerTest, InlineFunctionInDifferentCompilationUnit) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  // NOTE: inlineLTO_inlineA_qsort is only inlined with LTO/ThinLTO.
  call_inlineLTO_inlineA_qsort();
  symbolizer.symbolize(frames);

  expectFrameEq(
      frames.frames[6],
      "inlineLTO_inlineA_qsort",
      "folly::symbolizer::test::inlineLTO_inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils.cpp",
      kLineno_inlineA_qsort);
}

TEST(SymbolizerTest, InlineClassMemberFunctionSameFile) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_same_file_memberInline_inlineA_qsort();
  symbolizer.symbolize(frames);

  expectFrameEq(
      frames.frames[5],
      "inlineA_qsort",
      "folly::symbolizer::test::inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_qsort);

  expectFrameEq(
      frames.frames[6],
      "memberInline_inlineA_qsort",
      "folly::symbolizer::test::ClassSameFile::memberInline_inlineA_qsort() const",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils.cpp",
      kLineno_inlineA_qsort);
}

TEST(SymbolizerTest, StaticInlineClassMemberFunctionSameFile) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_same_file_staticMemberInline_inlineA_qsort();
  symbolizer.symbolize(frames);

  expectFrameEq(
      frames.frames[5],
      "inlineA_qsort",
      "folly::symbolizer::test::inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_qsort);

  expectFrameEq(
      frames.frames[6],
      "staticMemberInline_inlineA_qsort",
      "folly::symbolizer::test::ClassSameFile::staticMemberInline_inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils.cpp",
      kLineno_inlineA_qsort);
}

TEST(SymbolizerTest, InlineClassMemberFunctionInDifferentFile) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_different_file_memberInline_inlineA_qsort();
  symbolizer.symbolize(frames);

  expectFrameEq(
      frames.frames[5],
      "inlineA_qsort",
      "folly::symbolizer::test::inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_qsort);

  expectFrameEq(
      frames.frames[6],
      "memberInline_inlineA_qsort",
      "folly::symbolizer::test::ClassDifferentFile::memberInline_inlineA_qsort() const",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_inlineA_qsort);
}

TEST(SymbolizerTest, StaticInlineClassMemberFunctionInDifferentFile) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_different_file_staticMemberInline_inlineA_qsort();
  symbolizer.symbolize(frames);

  expectFrameEq(
      frames.frames[5],
      "inlineA_qsort",
      "folly::symbolizer::test::inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_qsort);

  expectFrameEq(
      frames.frames[6],
      "staticMemberInline_inlineA_qsort",
      "folly::symbolizer::test::ClassDifferentFile::staticMemberInline_inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_inlineA_qsort);
}

// No inline frames should be filled because of no extra frames.
TEST(SymbolizerTest, InlineFunctionBasicNoExtraFrames) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 100);
  FrameArray<9> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<9>;
  call_inlineB_inlineA_qsort();
  symbolizer.symbolize(frames);

  Symbolizer symbolizer2(nullptr, LocationInfoMode::FULL, 100);
  FrameArray<9> frames2;
  gComparatorGetStackTraceArg = &frames2;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<9>;
  call_inlineB_inlineA_qsort();
  symbolizer2.symbolize(frames2);

  expectFramesEq<9>(frames, frames2);
}

TEST(SymbolizerTest, InlineFunctionWithCache) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 100);

  FrameArray<100> frames;
  gComparatorGetStackTraceArg = &frames;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_inlineB_inlineA_qsort();
  symbolizer.symbolize(frames);

  expectFrameEq(
      frames.frames[5],
      "inlineA_qsort",
      "folly::symbolizer::test::inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_qsort);

  expectFrameEq(
      frames.frames[6],
      "inlineB_inlineA_qsort",
      "folly::symbolizer::test::inlineB_inlineA_qsort()",
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      kLineno_inlineA_qsort);

  FrameArray<100> frames2;
  gComparatorGetStackTraceArg = &frames2;
  gComparatorGetStackTrace = (bool (*)(void*))getStackTrace<100>;
  call_inlineB_inlineA_qsort();
  symbolizer.symbolize(frames2);
  expectFramesEq(frames, frames2);
}

int64_t functionWithTwoParameters(size_t a, int32_t b) {
  return a + b;
}

TEST(Dwarf, FindParameterNames) {
  SKIP_IF(!Symbolizer::isAvailable());

  auto address = reinterpret_cast<uintptr_t>(functionWithTwoParameters);
  Symbolizer symbolizer;
  SymbolizedFrame frame;
  ASSERT_TRUE(symbolizer.symbolize(address, frame));

  std::vector<folly::StringPiece> names;
  Dwarf dwarf(frame.file.get());
  LocationInfo info;
  folly::Range<SymbolizedFrame*> extraInlineFrames = {};
  dwarf.findAddress(
      frame.addr,
      LocationInfoMode::FAST,
      info,
      extraInlineFrames,
      [&](const folly::StringPiece name) { names.push_back(name); });

  ASSERT_EQ(2, names.size());
  ASSERT_EQ("a", names[0]);
  ASSERT_EQ("b", names[1]);
}

} // namespace test
} // namespace symbolizer
} // namespace folly

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

// Can't use initFacebookLight since that would install its own signal handlers
// Can't use initFacebookNoSignals since we cannot depend on common
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
