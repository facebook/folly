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

// ===== Cross-Engine: POSIX Character Classes =====

TEST(RegexCrossEngineTest, PosixCharClasses) {
  // [:alpha:]
  expectMatchAllEngines<"[[:alpha:]]">("a", true);
  expectMatchAllEngines<"[[:alpha:]]">("Z", true);
  expectMatchAllEngines<"[[:alpha:]]">("1", false);

  // [:digit:]
  expectMatchAllEngines<"[[:digit:]]">("5", true);
  expectMatchAllEngines<"[[:digit:]]">("a", false);

  // [:alnum:]
  expectMatchAllEngines<"[[:alnum:]]">("a", true);
  expectMatchAllEngines<"[[:alnum:]]">("5", true);
  expectMatchAllEngines<"[[:alnum:]]">("!", false);

  // [:upper:] / [:lower:]
  expectMatchAllEngines<"[[:upper:]]">("A", true);
  expectMatchAllEngines<"[[:upper:]]">("a", false);
  expectMatchAllEngines<"[[:lower:]]">("a", true);
  expectMatchAllEngines<"[[:lower:]]">("A", false);

  // [:space:] / [:blank:]
  expectMatchAllEngines<"[[:space:]]">(" ", true);
  expectMatchAllEngines<"[[:space:]]">("\t", true);
  expectMatchAllEngines<"[[:space:]]">("\n", true);
  expectMatchAllEngines<"[[:space:]]">("a", false);
  expectMatchAllEngines<"[[:blank:]]">(" ", true);
  expectMatchAllEngines<"[[:blank:]]">("\t", true);
  expectMatchAllEngines<"[[:blank:]]">("\n", false);

  // [:punct:]
  expectMatchAllEngines<"[[:punct:]]">("!", true);
  expectMatchAllEngines<"[[:punct:]]">(".", true);
  expectMatchAllEngines<"[[:punct:]]">("a", false);

  // [:xdigit:]
  expectMatchAllEngines<"[[:xdigit:]]">("a", true);
  expectMatchAllEngines<"[[:xdigit:]]">("F", true);
  expectMatchAllEngines<"[[:xdigit:]]">("9", true);
  expectMatchAllEngines<"[[:xdigit:]]">("g", false);

  // [:ascii:]
  expectMatchAllEngines<"[[:ascii:]]">("a", true);
  expectMatchAllEngines<"[[:ascii:]]">("\x7f", true);

  // [:cntrl:]
  expectMatchAllEngines<"[[:cntrl:]]">({"\x00", 1}, true);
  expectMatchAllEngines<"[[:cntrl:]]">("\x1f", true);
  expectMatchAllEngines<"[[:cntrl:]]">("a", false);

  // [:graph:] / [:print:]
  expectMatchAllEngines<"[[:graph:]]">("a", true);
  expectMatchAllEngines<"[[:graph:]]">(" ", false);
  expectMatchAllEngines<"[[:print:]]">("a", true);
  expectMatchAllEngines<"[[:print:]]">(" ", true);
  expectMatchAllEngines<"[[:print:]]">("\x01", false);
}

TEST(RegexCrossEngineTest, PosixClassCombinations) {
  // Mixed with ranges
  expectMatchAllEngines<"[[:digit:]a-f]">("5", true);
  expectMatchAllEngines<"[[:digit:]a-f]">("c", true);
  expectMatchAllEngines<"[[:digit:]a-f]">("g", false);

  // Multiple POSIX classes
  expectMatchAllEngines<"[[:alpha:][:digit:]]">("a", true);
  expectMatchAllEngines<"[[:alpha:][:digit:]]">("5", true);
  expectMatchAllEngines<"[[:alpha:][:digit:]]">("!", false);

  // Negated POSIX class
  expectMatchAllEngines<"[[:^alpha:]]">("1", true);
  expectMatchAllEngines<"[[:^alpha:]]">("a", false);

  // Negated bracket with POSIX class
  expectMatchAllEngines<"[^[:digit:]]">("a", true);
  expectMatchAllEngines<"[^[:digit:]]">("5", false);
}

TEST(RegexCrossEngineTest, PosixClassSearch) {
  expectSearchAllEngines<"[[:digit:]]+">("abc123def", true, "123");
  expectSearchAllEngines<"[[:alpha:]]+">("123abc456", true, "abc");
}
