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

#include <folly/regex/test/CrossEngineTestHelpers.h>

#include <string>
#include <vector>

#include <folly/portability/GTest.h>
#include <folly/regex/Regex.h>

using namespace folly::regex;
using namespace folly::regex::testing;

// ===== Cross-Engine: String-Level Anchors =====

TEST(RegexCrossEngineTest, StartOfStringAnchor) {
  expectSearchAllEngines<R"(\Afoo)">("foobar", true, "foo");
  expectSearchAllEngines<R"(\Afoo)">("barfoo", false);
}

TEST(RegexCrossEngineTest, EndOfStringAnchor) {
  expectSearchAllEngines<R"(foo\z)">("barfoo", true, "foo");
  expectSearchAllEngines<R"(foo\z)">("foobar", false);
}

TEST(RegexCrossEngineTest, EndOfStringOrNewlineAnchor) {
  expectSearchAllEngines<R"(foo\Z)">("barfoo", true, "foo");
  expectSearchAllEngines<R"(foo\Z)">("barfoo\n", true, "foo");
  expectSearchAllEngines<R"(foo\Z)">("foobar", false);
}

// ===== Cross-Engine: AnyByte =====

TEST(RegexCrossEngineTest, AnyByteMatchesNewline) {
  expectMatchAllEngines<R"(a\Cb)">("a\nb", true);
  expectMatchAllEngines<R"(a\Cb)">("axb", true);
}

TEST(RegexCrossEngineTest, AnyByteVsDot) {
  expectMatchAllEngines<"a.b">("a\nb", false);
  expectMatchAllEngines<R"(a\Cb)">("a\nb", true);
}

// ===== Cross-Engine: Multiline Flag =====

TEST(RegexCrossEngineTest, MultilineBeginAnchor) {
  expectSearchAllEngines<"^line", Flags::Multiline>(
      "first\nline two", true, "line");
  expectSearchAllEngines<"^line", Flags::Multiline>("line one", true, "line");
}

TEST(RegexCrossEngineTest, MultilineEndAnchor) {
  expectSearchAllEngines<"end$", Flags::Multiline>(
      "the end\nnext line", true, "end");
  expectSearchAllEngines<"end$", Flags::Multiline>("the end", true, "end");
}

TEST(RegexCrossEngineTest, MultilineDoesNotAffectStringAnchors) {
  expectSearchAllEngines<R"(\Afirst)", Flags::Multiline>(
      "first\nsecond", true, "first");
  expectSearchAllEngines<R"(\Afirst)", Flags::Multiline>(
      "second\nfirst", false);

  expectSearchAllEngines<R"(last\z)", Flags::Multiline>(
      "first\nlast", true, "last");
  expectSearchAllEngines<R"(last\z)", Flags::Multiline>("last\nfirst", false);
}

// ===== Cross-Engine: DotAll Flag =====

TEST(RegexCrossEngineTest, DotAllDotMatchesNewline) {
  expectMatchAllEngines<"a.b", Flags::DotAll>("a\nb", true);
  expectMatchAllEngines<"a.b", Flags::DotAll>("axb", true);
}

TEST(RegexCrossEngineTest, DotAllDefault) {
  expectMatchAllEngines<"a.b">("a\nb", false);
  expectMatchAllEngines<"a.b">("axb", true);
}
