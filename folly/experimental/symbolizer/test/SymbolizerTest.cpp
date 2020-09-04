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
#include <folly/experimental/symbolizer/test/SymbolizerTestUtils.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

namespace folly {
namespace symbolizer {
namespace test {

void foo() {}

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
FOLLY_NOINLINE void bar();

void bar(FrameArray<100>& frames) {
  framesToFill = &frames;
  std::array<int, 2> a = {1, 2};
  // Use qsort, which is in a different library
  qsort(a.data(), 2, sizeof(int), comparator<100>);
  framesToFill = nullptr;
}

class ElfCacheTest : public testing::Test {
 protected:
  void SetUp() override;
};

// Capture "golden" stack trace with default-configured Symbolizer
FrameArray<100> goldenFrames;

void ElfCacheTest::SetUp() {
  SKIP_IF(!Symbolizer::isAvailable());

  bar(goldenFrames);
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
  bar(frames);
  symbolizer.symbolize(frames);

  FrameArray<100> frames2;
  bar(frames2);
  symbolizer.symbolize(frames2);
  for (size_t i = 0; i < frames.frameCount; i++) {
    EXPECT_STREQ(frames.frames[i].name, frames2.frames[i].name);
  }
}

namespace {

template <size_t kNumFrames>
FOLLY_NOINLINE void lexicalBlockBar(FrameArray<kNumFrames>& frames) try {
  size_t unused = 0;
  unused++;
  kInlineBarCallByLexicalBarLineNo = __LINE__ + 1;
  inlineBar(frames);
} catch (...) {
  folly::assume_unreachable();
}

void verifyStackTrace(
    Symbolizer& symbolizer,
    const FrameArray<100>& frames,
    const std::string& barName,
    size_t barLine,
    const std::string& barFile) {
  EXPECT_EQ(
      "void folly::symbolizer::test::inlineFoo<100ul>("
      "folly::symbolizer::FrameArray<100ul>&)",
      std::string(folly::demangle(frames.frames[5].name)));
  EXPECT_EQ(
      "./folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      std::string(frames.frames[5].location.file.toString()));
  EXPECT_EQ(kQsortCallLineNo, frames.frames[5].location.line);
  EXPECT_EQ(barName, std::string(folly::demangle(frames.frames[6].name)));
  EXPECT_EQ(barFile, std::string(frames.frames[6].location.file.toString()));
  EXPECT_EQ(barLine, frames.frames[6].location.line);

  FrameArray<10> singleAddressFrames;
  singleAddressFrames.frameCount = 1;
  singleAddressFrames.addresses[0] = frames.frames[7].addr;
  // Two inline function calls are added into frames.
  EXPECT_EQ(3, symbolizer.symbolize(singleAddressFrames));
}

template <size_t kNumFrames = 100>
void compareFrames(
    const FrameArray<kNumFrames>& frames1,
    const FrameArray<kNumFrames>& frames2) {
  EXPECT_EQ(frames1.frameCount, frames2.frameCount);
  for (size_t i = 0; i < frames1.frameCount; i++) {
    EXPECT_STREQ(frames1.frames[i].name, frames2.frames[i].name);
  }
}

} // namespace

class ClassWithInlineFunctions {
 public:
  FOLLY_ALWAYS_INLINE void inlineBar(FrameArray<100>& frames) const {
    kFooCallByClassBarLineNo = __LINE__ + 1;
    inlineFoo(frames);
  }

  FOLLY_ALWAYS_INLINE static void staticInlineBar(FrameArray<100>& frames) {
    kFooCallByClassStaticBarLineNo = __LINE__ + 1;
    inlineFoo(frames);
  }

  // Dummy non-inline function.
  size_t dummy() const {
    return dummy_;
  }

  size_t dummy_ = 0;
};

TEST(SymbolizerTest, InlineFunctionBasic) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  inlineBar<100>(frames);
  symbolizer.symbolize(frames);

  // clang-format off
  // Expected full stack trace with @mode/dev. The last frame is missing in opt
  // mode.
  //  Frame: _ZN5folly10symbolizer13getStackTraceEPmm
  //  Frame: _ZN5folly10symbolizer13getStackTraceILm100EEEbRNS0_10FrameArrayIXT_EEE
  //  Frame: _ZN5folly10symbolizer4test10comparatorILm100EEEiPKvS4_
  //  Frame: msort_with_tmp.part.0
  //  Frame: __GI___qsort_r
  //  Frame: _ZN5folly10symbolizer4test9inlineFooILm100EEEvRNS0_10FrameArrayIXT_EEE
  //  Frame: _ZN5folly10symbolizer4test12_GLOBAL__N_19inlineBarILm100EEEvRNS0_10FrameArrayIXT_EEE
  //  Frame: _ZN5folly10symbolizer4test39SymbolizerTest_InlineFunctionBasic_Test8TestBodyEv
  //  Frame: _ZN7testing8internal35HandleExceptionsInMethodIfSupportedINS_4TestEvEET0_PT_MS4_FS3_vEPKc
  //  Frame: _ZN7testing4Test3RunEv
  //  Frame: _ZN7testing8TestInfo3RunEv
  //  Frame: _ZN7testing8TestCase3RunEv
  //  Frame: _ZN7testing8internal12UnitTestImpl11RunAllTestsEv
  // clang-format on
  verifyStackTrace(
      symbolizer,
      frames,
      "void folly::symbolizer::test::inlineBar<100ul>("
      "folly::symbolizer::FrameArray<100ul>&)",
      kFooCallByStandaloneBarLineNo,
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h");

  FrameArray<100> frames2;
  inlineBar<100>(frames2);
  symbolizer.symbolize(frames2);

  compareFrames(frames, frames2);
}

TEST(SymbolizerTest, InlineFunctionWithoutEnoughFrames) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  lexicalBlockBar<100>(frames);
  symbolizer.symbolize(frames);

  // The address of the line where lexicalBlockBar calls inlineBar.
  uintptr_t address = frames.frames[7].addr;
  std::array<SymbolizedFrame, 2> limitedFrames;
  symbolizer.symbolize(
      folly::Range<const uintptr_t*>(&address, 1), folly::range(limitedFrames));

  EXPECT_EQ(
      "void folly::symbolizer::test::inlineBar<100ul>("
      "folly::symbolizer::FrameArray<100ul>&)",
      std::string(folly::demangle(limitedFrames[0].name)));
  EXPECT_EQ(
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h",
      std::string(limitedFrames[0].location.file.toString()));
  EXPECT_EQ(kFooCallByStandaloneBarLineNo, limitedFrames[0].location.line);

  EXPECT_EQ(
      "void folly::symbolizer::test::(anonymous namespace)::lexicalBlockBar"
      "<100ul>(folly::symbolizer::FrameArray<100ul>&)",
      std::string(folly::demangle(limitedFrames[1].name)));
  EXPECT_EQ(
      "folly/experimental/symbolizer/test/SymbolizerTest.cpp",
      std::string(limitedFrames[1].location.file.toString()));
  EXPECT_EQ(kInlineBarCallByLexicalBarLineNo, limitedFrames[1].location.line);
}

TEST(SymbolizerTest, InlineFunctionInLexicalBlock) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  lexicalBlockBar<100>(frames);
  symbolizer.symbolize(frames);

  verifyStackTrace(
      symbolizer,
      frames,
      "void folly::symbolizer::test::inlineBar<100ul>("
      "folly::symbolizer::FrameArray<100ul>&)",
      kFooCallByStandaloneBarLineNo,
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h");

  EXPECT_EQ(
      "void folly::symbolizer::test::(anonymous namespace)::lexicalBlockBar"
      "<100ul>(folly::symbolizer::FrameArray<100ul>&)",
      std::string(folly::demangle(frames.frames[7].name)));
  EXPECT_EQ(
      "folly/experimental/symbolizer/test/SymbolizerTest.cpp",
      std::string(frames.frames[7].location.file.toString()));
  EXPECT_EQ(kInlineBarCallByLexicalBarLineNo, frames.frames[7].location.line);
}

TEST(SymbolizerTest, InlineFunctionInDifferentCompilationUnit) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  // NOTE: inlineBaz is only inlined with thinlto compilation mode enabled.
  inlineBaz(frames);
  symbolizer.symbolize(frames);

  EXPECT_EQ(
      "folly::symbolizer::test::inlineBaz("
      "folly::symbolizer::FrameArray<100ul>&)",
      std::string(folly::demangle(frames.frames[6].name)));
  EXPECT_EQ(
      "folly/experimental/symbolizer/test/SymbolizerTestUtils.cpp",
      std::string(frames.frames[6].location.file.toString()));
  EXPECT_EQ(kFooCallByStandaloneBazLineNo, frames.frames[6].location.line);
}

TEST(SymbolizerTest, InlineClassMemberFunction) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  ClassWithInlineFunctions obj;
  obj.inlineBar(frames);
  symbolizer.symbolize(frames);

  verifyStackTrace(
      symbolizer,
      frames,
      "folly::symbolizer::test::ClassWithInlineFunctions::inlineBar("
      "folly::symbolizer::FrameArray<100ul>&) const",
      kFooCallByClassBarLineNo,
      "folly/experimental/symbolizer/test/SymbolizerTest.cpp");
}

TEST(SymbolizerTest, StaticInlineClassMemberFunction) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  ClassWithInlineFunctions::staticInlineBar(frames);
  symbolizer.symbolize(frames);

  verifyStackTrace(
      symbolizer,
      frames,
      "folly::symbolizer::test::ClassWithInlineFunctions::staticInlineBar("
      "folly::symbolizer::FrameArray<100ul>&)",
      kFooCallByClassStaticBarLineNo,
      "folly/experimental/symbolizer/test/SymbolizerTest.cpp");
}

TEST(SymbolizerTest, InlineClassMemberFunctionInDifferentFile) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  InlineFunctionsWrapper obj;
  obj.inlineBar(frames);
  symbolizer.symbolize(frames);

  verifyStackTrace(
      symbolizer,
      frames,
      "folly::symbolizer::test::InlineFunctionsWrapper::inlineBar("
      "folly::symbolizer::FrameArray<100ul>&) const",
      kFooCallByClassInDifferentFileBarLineNo,
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h");
}

TEST(SymbolizerTest, StaticInlineClassMemberFunctionInDifferentFile) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 0);

  FrameArray<100> frames;
  InlineFunctionsWrapper::staticInlineBar(frames);
  symbolizer.symbolize(frames);

  verifyStackTrace(
      symbolizer,
      frames,
      "folly::symbolizer::test::InlineFunctionsWrapper::staticInlineBar("
      "folly::symbolizer::FrameArray<100ul>&)",
      kFooCallByClassInDifferentFileStaticBarLineNo,
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h");
}

// No inline frames should be filled because of no extra frames.
TEST(SymbolizerTest, InlineFunctionBasicNoExtraFrames) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 100);
  FrameArray<8> frames;
  inlineBar<8>(frames);
  symbolizer.symbolize(frames);

  Symbolizer symbolizer2(nullptr, LocationInfoMode::FULL, 100);
  FrameArray<8> frames2;
  inlineBar<8>(frames2);
  symbolizer2.symbolize(frames2);

  compareFrames<8>(frames, frames2);
}

TEST(SymbolizerTest, InlineFunctionWithCache) {
  SKIP_IF(!Symbolizer::isAvailable());

  Symbolizer symbolizer(nullptr, LocationInfoMode::FULL_WITH_INLINE, 100);

  FrameArray<100> frames;
  inlineBar<100>(frames);
  symbolizer.symbolize(frames);

  verifyStackTrace(
      symbolizer,
      frames,
      "void folly::symbolizer::test::inlineBar<100ul>("
      "folly::symbolizer::FrameArray<100ul>&)",
      kFooCallByStandaloneBarLineNo,
      "folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h");

  FrameArray<100> frames2;
  inlineBar<100>(frames2);
  symbolizer.symbolize(frames2);
  compareFrames(frames, frames2);
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
      address,
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

// Can't use initFacebookLight since that would install its own signal handlers
// Can't use initFacebookNoSignals since we cannot depend on common
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
