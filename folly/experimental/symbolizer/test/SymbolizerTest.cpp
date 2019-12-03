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

#include <folly/Range.h>
#include <folly/String.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace symbolizer {
namespace test {

void foo() {}

TEST(Symbolizer, Single) {
  // It looks like we could only use .debug_aranges with "-g2", with
  // "-g1 -gdwarf-aranges", the code has to fallback to line-tables to
  // get the file name.
  Symbolizer symbolizer(Dwarf::LocationInfoMode::FULL);
  SymbolizedFrame a;
  ASSERT_TRUE(symbolizer.symbolize(reinterpret_cast<uintptr_t>(foo), a));
  EXPECT_EQ("folly::symbolizer::test::foo()", a.demangledName());

  auto path = a.location.file.toString();
  folly::StringPiece basename(path);
  auto pos = basename.rfind('/');
  if (pos != folly::StringPiece::npos) {
    basename.advance(pos + 1);
  }
  EXPECT_EQ("SymbolizerTest.cpp", basename.str());
}

void* framesToFill{nullptr};

template <size_t kNumFrames = 100>
int comparator(const void* ap, const void* bp) {
  getStackTrace(*static_cast<FrameArray<kNumFrames>*>(framesToFill));

  int a = *static_cast<const int*>(ap);
  int b = *static_cast<const int*>(bp);
  return a < b ? -1 : a > b ? 1 : 0;
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

TEST_F(ElfCacheTest, TinyElfCache) {
  ElfCache cache(1);
  Symbolizer symbolizer(&cache);
  // Run twice, in case the wrong stuff gets evicted?
  for (size_t i = 0; i < 2; ++i) {
    runElfCacheTest(symbolizer);
  }
}

TEST_F(ElfCacheTest, SignalSafeElfCache) {
  SignalSafeElfCache cache(100);
  Symbolizer symbolizer(&cache);
  for (size_t i = 0; i < 2; ++i) {
    runElfCacheTest(symbolizer);
  }
}

TEST(SymbolizerTest, SymbolCache) {
  Symbolizer symbolizer(nullptr, Dwarf::LocationInfoMode::FULL, 100);

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

template <size_t kNumFrames = 100>
FOLLY_ALWAYS_INLINE void inlineFoo(FrameArray<kNumFrames>& frames) {
  framesToFill = &frames;
  std::array<int, 2> a = {1, 2};
  // Use qsort, which is in a different library
  qsort(a.data(), 2, sizeof(int), comparator<kNumFrames>);
  framesToFill = nullptr;
}

template <size_t kNumFrames = 100>
FOLLY_ALWAYS_INLINE void inlineBar(FrameArray<kNumFrames>& frames) {
  inlineFoo(frames);
}

} // namespace

TEST(SymbolizerTest, InlineFunctionBasic) {
  Symbolizer symbolizer(
      nullptr, Dwarf::LocationInfoMode::FULL_WITH_INLINE, 100);

  FrameArray<100> frames;
  inlineBar<100>(frames);
  symbolizer.symbolize(frames);

  EXPECT_EQ("inlineFoo<100>", std::string(frames.frames[5].name));
  EXPECT_EQ(
      "folly/experimental/symbolizer/test/SymbolizerTest.cpp",
      std::string(frames.frames[5].location.file.toString()));
  EXPECT_EQ(139, frames.frames[5].location.line);
  EXPECT_EQ("inlineBar<100>", std::string(frames.frames[6].name));
  EXPECT_EQ(
      "folly/experimental/symbolizer/test/SymbolizerTest.cpp",
      std::string(frames.frames[6].location.file.toString()));
  EXPECT_EQ(145, frames.frames[6].location.line);

  FrameArray<100> frames2;
  inlineBar<100>(frames2);
  symbolizer.symbolize(frames2);
  for (size_t i = 0; i < frames.frameCount; i++) {
    EXPECT_STREQ(frames.frames[i].name, frames2.frames[i].name);
  }
}

// No inline frames should be filled because of no extra frames.
TEST(SymbolizerTest, InlineFunctionBasicNoExtraFrames) {
  Symbolizer symbolizer(
      nullptr, Dwarf::LocationInfoMode::FULL_WITH_INLINE, 100);
  FrameArray<8> frames;
  inlineBar<8>(frames);
  symbolizer.symbolize(frames);

  Symbolizer symbolizer2(nullptr, Dwarf::LocationInfoMode::FULL, 100);
  FrameArray<8> frames2;
  inlineBar<8>(frames2);
  symbolizer2.symbolize(frames2);

  for (size_t i = 0; i < frames.frameCount; i++) {
    EXPECT_STREQ(frames.frames[i].name, frames2.frames[i].name);
  }
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
