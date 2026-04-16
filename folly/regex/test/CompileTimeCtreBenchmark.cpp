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

// Compile-time stress test: instantiates a large number of CTRE patterns
// to measure C++ compilation overhead. Patterns are sourced from existing
// benchmarks and tests across the folly/regex test suite, filtered to only
// include features that CTRE supports (no lookahead/lookbehind,
// backreferences, possessive quantifiers, word boundaries, POSIX classes,
// or \A/\z/\Z/\C/\a anchors).

#include <string>

#include <ctre.hpp>
#include <folly/BenchmarkUtil.h>

int main() {
  const std::string s =
      "hello world abc 12345 user@example.com version 1.23.456 "
      "the quick brown fox 2024-01-15 10:30:00 devgpu production "
      "IDENTITY_TYPE 192.168.1.1 abcdef123x com.facebook.orca-fg-12345 "
      "error warning alpha bravo charlie top_rank.dp12.num_models ";
  int sink = 1;
  (void)sink;

  // ===== Simple literals =====
  folly::doNotOptimizeAway(ctre::search<"hello">(s));
  folly::doNotOptimizeAway(ctre::search<"abc">(s));
  folly::doNotOptimizeAway(ctre::search<"world">(s));

  // ===== Dot / any char =====
  folly::doNotOptimizeAway(ctre::search<"a.b">(s));
  folly::doNotOptimizeAway(ctre::search<"a.*b">(s));
  folly::doNotOptimizeAway(ctre::search<"a.*?b">(s));
  folly::doNotOptimizeAway(ctre::search<".*x">(s));

  // ===== Character classes =====
  folly::doNotOptimizeAway(ctre::search<"[abc]">(s));
  folly::doNotOptimizeAway(ctre::search<"[a-z]+">(s));
  folly::doNotOptimizeAway(ctre::search<"[^0-9]+">(s));
  folly::doNotOptimizeAway(ctre::search<"[a-zA-Z0-9]+">(s));
  folly::doNotOptimizeAway(ctre::search<"[A-Z_0-9]+">(s));
  folly::doNotOptimizeAway(ctre::search<"[-a]">(s));
  folly::doNotOptimizeAway(ctre::search<"[^abc]">(s));
  folly::doNotOptimizeAway(ctre::search<"[^a-z]">(s));
  folly::doNotOptimizeAway(ctre::search<"[^a-z]+">(s));
  folly::doNotOptimizeAway(ctre::search<"[a-z ,.]+">(s));
  folly::doNotOptimizeAway(
      ctre::search<R"([a-zA-Z0-9_.\-]*(/[a-zA-Z0-9_.\-]+)*)">(s));
  folly::doNotOptimizeAway(ctre::search<R"([a-zA-Z0-9_.:/\-|]+)">(s));

  // ===== Shorthand character classes =====
  folly::doNotOptimizeAway(ctre::search<R"(\d+)">(s));
  folly::doNotOptimizeAway(ctre::search<R"(\w+)">(s));
  folly::doNotOptimizeAway(ctre::search<R"(\s+)">(s));
  folly::doNotOptimizeAway(ctre::search<R"(\D+)">(s));
  folly::doNotOptimizeAway(ctre::search<R"(\W+)">(s));
  folly::doNotOptimizeAway(ctre::search<R"(\S+)">(s));

  // ===== Quantifiers =====
  folly::doNotOptimizeAway(ctre::search<"ab*c">(s));
  folly::doNotOptimizeAway(ctre::search<"ab+c">(s));
  folly::doNotOptimizeAway(ctre::search<"ab?c">(s));
  folly::doNotOptimizeAway(ctre::search<"a{2,4}">(s));
  folly::doNotOptimizeAway(ctre::search<"a{3}">(s));
  folly::doNotOptimizeAway(ctre::search<"a{2,}">(s));
  folly::doNotOptimizeAway(ctre::search<"a*">(s));

  // ===== Lazy quantifiers =====
  folly::doNotOptimizeAway(ctre::search<"[a-z]+?">(s));
  folly::doNotOptimizeAway(ctre::search<"a??">(s));
  folly::doNotOptimizeAway(ctre::search<"a{2,5}?">(s));
  folly::doNotOptimizeAway(ctre::search<R"([a-z]+?\d)">(s));
  folly::doNotOptimizeAway(ctre::search<"a+?a">(s));

  // ===== Alternation =====
  folly::doNotOptimizeAway(ctre::search<"cat|dog">(s));
  folly::doNotOptimizeAway(ctre::search<"quick|slow|fast|lazy">(s));
  folly::doNotOptimizeAway(ctre::search<"cat|dog|bird">(s));
  folly::doNotOptimizeAway(
      ctre::search<"error|warning|info|debug|trace|fatal|critical">(s));
  folly::doNotOptimizeAway(
      ctre::search<"GET|POST|PUT|DELETE|PATCH|HEAD|OPTIONS">(s));
  folly::doNotOptimizeAway(
      ctre::search<
          "alpha|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet">(s));
  folly::doNotOptimizeAway(
      ctre::search<"(dev|devvm|devrs|devbig|devgpu|shellserver)">(s));
  folly::doNotOptimizeAway(ctre::search<"a|b|c|d">(s));
  folly::doNotOptimizeAway(ctre::search<"abc|def|ghi">(s));
  folly::doNotOptimizeAway(ctre::search<"foobar|foobaz|fooqux">(s));
  folly::doNotOptimizeAway(ctre::search<"dev|devvm|devrs">(s));

  // ===== Groups =====
  folly::doNotOptimizeAway(ctre::search<R"((\d+))">(s));
  folly::doNotOptimizeAway(ctre::search<R"((\d+)-(\w+))">(s));
  folly::doNotOptimizeAway(ctre::search<R"(((\d+)-(\w+)))">(s));
  folly::doNotOptimizeAway(ctre::search<"(?:ab)+">(s));
  folly::doNotOptimizeAway(ctre::search<R"((\d+)\.(\d+)\.(\d+))">(s));
  folly::doNotOptimizeAway(ctre::search<R"((\w+)@(\w+)\.(\w+))">(s));
  folly::doNotOptimizeAway(ctre::search<R"(_ct(\d+))">(s));
  folly::doNotOptimizeAway(ctre::search<R"(_s(\d+))">(s));
  folly::doNotOptimizeAway(ctre::search<R"((.+)-fg-(\d+))">(s));
  folly::doNotOptimizeAway(ctre::search<R"((.+)-bg-(\d+))">(s));
  folly::doNotOptimizeAway(ctre::search<"(?:(?:a))">(s));
  folly::doNotOptimizeAway(ctre::search<"(?:(?:(?:abc)))">(s));
  folly::doNotOptimizeAway(ctre::search<"(a)(b)(c)">(s));
  folly::doNotOptimizeAway(ctre::search<"(a)(?:b)(c)">(s));
  folly::doNotOptimizeAway(ctre::search<"((a)(b))">(s));

  // ===== Anchors =====
  folly::doNotOptimizeAway(ctre::search<"^hello$">(s));
  folly::doNotOptimizeAway(ctre::search<"^hello">(s));
  folly::doNotOptimizeAway(ctre::search<"^start$">(s));

  // ===== Escape sequences =====
  folly::doNotOptimizeAway(ctre::search<R"(a\.b)">(s));
  folly::doNotOptimizeAway(ctre::search<R"(a\tb)">(s));
  folly::doNotOptimizeAway(ctre::search<R"(\(\)\[\])">(s));
  folly::doNotOptimizeAway(ctre::search<R"(\x41)">(s));
  folly::doNotOptimizeAway(ctre::search<R"([\x41-\x5A]+)">(s));

  // ===== Email patterns =====
  folly::doNotOptimizeAway(ctre::search<R"(\w+@\w+\.\w+)">(s));
  folly::doNotOptimizeAway(
      ctre::search<R"([a-zA-Z0-9.]+@[a-zA-Z0-9]+\.[a-zA-Z]{2,4})">(s));

  // ===== Version / IP / Date patterns =====
  folly::doNotOptimizeAway(ctre::search<R"(\d+\.\d+\.\d+)">(s));
  folly::doNotOptimizeAway(
      ctre::search<R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})">(s));
  folly::doNotOptimizeAway(ctre::search<R"(\d{4}-\d{2}-\d{2})">(s));
  folly::doNotOptimizeAway(
      ctre::search<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})">(s));
  folly::doNotOptimizeAway(
      ctre::search<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[\w+\])">(s));
  folly::doNotOptimizeAway(
      ctre::search<
          R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})">(
          s));
  folly::doNotOptimizeAway(
      ctre::search<R"(top_rank\.dp([1-9][0-9]*)\.num_models)">(s));

  // ===== Backtracking-heavy / nested quantifiers =====
  folly::doNotOptimizeAway(ctre::search<"(a+)+b">(s));
  folly::doNotOptimizeAway(ctre::search<"(a*)*b">(s));
  folly::doNotOptimizeAway(ctre::search<"(x+x+)+y">(s));
  folly::doNotOptimizeAway(ctre::search<"(a|a)+b">(s));
  folly::doNotOptimizeAway(ctre::search<R"((\w+|\d+)+z)">(s));
  folly::doNotOptimizeAway(ctre::search<"(ab|a)+c">(s));
  folly::doNotOptimizeAway(ctre::search<R"((?:\w+|\d+)+z)">(s));
  folly::doNotOptimizeAway(ctre::search<R"((?:a*)*b)">(s));
  folly::doNotOptimizeAway(ctre::search<R"((?:a+)*b)">(s));
  folly::doNotOptimizeAway(ctre::search<R"((?:a*)+b)">(s));

  // ===== Disjoint patterns =====
  folly::doNotOptimizeAway(ctre::search<"[a-z]+[0-9]*[a-z]+">(s));
  folly::doNotOptimizeAway(ctre::search<"[a-z]+[0-9]+x">(s));
  folly::doNotOptimizeAway(ctre::search<R"(\w+\s+\w+)">(s));
  folly::doNotOptimizeAway(ctre::search<"[a-z]+[0-9]+">(s));

  // ===== Date parsing / structured bindings =====
  folly::doNotOptimizeAway(ctre::search<R"((\d{4})-(\d{2})-(\d{2}))">(s));
  folly::doNotOptimizeAway(ctre::search<R"((\w+)@(\w+))">(s));

  // ===== Additional patterns from tests =====
  folly::doNotOptimizeAway(ctre::search<R"([a-z]{3}\d{3})">(s));
  folly::doNotOptimizeAway(ctre::search<R"([a-z][a-z][a-z]\d)">(s));
  folly::doNotOptimizeAway(ctre::search<R"((\d+)\.(\d+))">(s));
  folly::doNotOptimizeAway(ctre::search<"[a-m]|[n-z]">(s));
  folly::doNotOptimizeAway(ctre::search<"a|[b-d]|e">(s));
  folly::doNotOptimizeAway(ctre::search<"^foo|^bar">(s));
  folly::doNotOptimizeAway(ctre::search<"foo$|bar$">(s));
  folly::doNotOptimizeAway(ctre::search<"a+a+b">(s));
  folly::doNotOptimizeAway(ctre::search<"a+b+c">(s));
  folly::doNotOptimizeAway(ctre::search<"(?:a|ab)+c">(s));
  folly::doNotOptimizeAway(ctre::search<"abc.*xyz">(s));
  folly::doNotOptimizeAway(ctre::search<"a{1}">(s));
  folly::doNotOptimizeAway(ctre::search<"ab{1}c">(s));

  return 0;
}
