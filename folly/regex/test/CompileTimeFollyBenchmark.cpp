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

// Compile-time stress test: instantiates a large number of folly::regex
// patterns to measure C++ compilation overhead. The pattern set here is
// identical to CompileTimeCtreBenchmark.cpp for a fair comparison.

#include <string>

#include <folly/BenchmarkUtil.h>
#include <folly/regex/Regex.h>

using namespace folly::regex;

int main() {
  const std::string s =
      "hello world abc 12345 user@example.com version 1.23.456 "
      "the quick brown fox 2024-01-15 10:30:00 devgpu production "
      "IDENTITY_TYPE 192.168.1.1 abcdef123x com.facebook.orca-fg-12345 "
      "error warning alpha bravo charlie top_rank.dp12.num_models ";
  volatile int sink = 1;
  (void)sink;

  // ===== Simple literals =====
  folly::doNotOptimizeAway(match<"hello">(s));
  folly::doNotOptimizeAway(match<"abc">(s));
  folly::doNotOptimizeAway(match<"world">(s));

  // ===== Dot / any char =====
  folly::doNotOptimizeAway(match<"a.b">(s));
  folly::doNotOptimizeAway(match<"a.*b">(s));
  folly::doNotOptimizeAway(match<"a.*?b">(s));
  folly::doNotOptimizeAway(match<".*x">(s));

  // ===== Character classes =====
  folly::doNotOptimizeAway(match<"[abc]">(s));
  folly::doNotOptimizeAway(match<"[a-z]+">(s));
  folly::doNotOptimizeAway(match<"[^0-9]+">(s));
  folly::doNotOptimizeAway(match<"[a-zA-Z0-9]+">(s));
  folly::doNotOptimizeAway(match<"[A-Z_0-9]+">(s));
  folly::doNotOptimizeAway(match<"[-a]">(s));
  folly::doNotOptimizeAway(match<"[^abc]">(s));
  folly::doNotOptimizeAway(match<"[^a-z]">(s));
  folly::doNotOptimizeAway(match<"[^a-z]+">(s));
  folly::doNotOptimizeAway(match<"[a-z ,.]+">(s));
  folly::doNotOptimizeAway(match<R"([a-zA-Z0-9_.\-]*(/[a-zA-Z0-9_.\-]+)*)">(s));
  folly::doNotOptimizeAway(match<R"([a-zA-Z0-9_.:/\-|]+)">(s));

  // ===== Shorthand character classes =====
  folly::doNotOptimizeAway(match<R"(\d+)">(s));
  folly::doNotOptimizeAway(match<R"(\w+)">(s));
  folly::doNotOptimizeAway(match<R"(\s+)">(s));
  folly::doNotOptimizeAway(match<R"(\D+)">(s));
  folly::doNotOptimizeAway(match<R"(\W+)">(s));
  folly::doNotOptimizeAway(match<R"(\S+)">(s));

  // ===== Quantifiers =====
  folly::doNotOptimizeAway(match<"ab*c">(s));
  folly::doNotOptimizeAway(match<"ab+c">(s));
  folly::doNotOptimizeAway(match<"ab?c">(s));
  folly::doNotOptimizeAway(match<"a{2,4}">(s));
  folly::doNotOptimizeAway(match<"a{3}">(s));
  folly::doNotOptimizeAway(match<"a{2,}">(s));
  folly::doNotOptimizeAway(match<"a*">(s));

  // ===== Lazy quantifiers =====
  folly::doNotOptimizeAway(match<"[a-z]+?">(s));
  folly::doNotOptimizeAway(match<"a??">(s));
  folly::doNotOptimizeAway(match<"a{2,5}?">(s));
  folly::doNotOptimizeAway(match<R"([a-z]+?\d)">(s));
  folly::doNotOptimizeAway(match<"a+?a">(s));

  // ===== Alternation =====
  folly::doNotOptimizeAway(match<"cat|dog">(s));
  folly::doNotOptimizeAway(match<"quick|slow|fast|lazy">(s));
  folly::doNotOptimizeAway(match<"cat|dog|bird">(s));
  folly::doNotOptimizeAway(
      test<"error|warning|info|debug|trace|fatal|critical">(s));
  folly::doNotOptimizeAway(match<"GET|POST|PUT|DELETE|PATCH|HEAD|OPTIONS">(s));
  folly::doNotOptimizeAway(
      test<"alpha|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet">(
          s));
  folly::doNotOptimizeAway(
      test<"(dev|devvm|devrs|devbig|devgpu|shellserver)">(s));
  folly::doNotOptimizeAway(match<"a|b|c|d">(s));
  folly::doNotOptimizeAway(match<"abc|def|ghi">(s));
  folly::doNotOptimizeAway(match<"foobar|foobaz|fooqux">(s));
  folly::doNotOptimizeAway(match<"dev|devvm|devrs">(s));

  // ===== Groups =====
  folly::doNotOptimizeAway(match<R"((\d+))">(s));
  folly::doNotOptimizeAway(match<R"((\d+)-(\w+))">(s));
  folly::doNotOptimizeAway(match<R"(((\d+)-(\w+)))">(s));
  folly::doNotOptimizeAway(match<"(?:ab)+">(s));
  folly::doNotOptimizeAway(match<R"((\d+)\.(\d+)\.(\d+))">(s));
  folly::doNotOptimizeAway(match<R"((\w+)@(\w+)\.(\w+))">(s));
  folly::doNotOptimizeAway(match<R"(_ct(\d+))">(s));
  folly::doNotOptimizeAway(match<R"(_s(\d+))">(s));
  folly::doNotOptimizeAway(match<R"((.+)-fg-(\d+))">(s));
  folly::doNotOptimizeAway(match<R"((.+)-bg-(\d+))">(s));
  folly::doNotOptimizeAway(match<"(?:(?:a))">(s));
  folly::doNotOptimizeAway(match<"(?:(?:(?:abc)))">(s));
  folly::doNotOptimizeAway(match<"(a)(b)(c)">(s));
  folly::doNotOptimizeAway(match<"(a)(?:b)(c)">(s));
  folly::doNotOptimizeAway(match<"((a)(b))">(s));

  // ===== Anchors =====
  folly::doNotOptimizeAway(match<"^hello$">(s));
  folly::doNotOptimizeAway(match<"^hello">(s));
  folly::doNotOptimizeAway(match<"^start$">(s));

  // ===== Escape sequences =====
  folly::doNotOptimizeAway(match<R"(a\.b)">(s));
  folly::doNotOptimizeAway(match<R"(a\tb)">(s));
  folly::doNotOptimizeAway(match<R"(\(\)\[\])">(s));
  folly::doNotOptimizeAway(match<R"(\x41)">(s));
  folly::doNotOptimizeAway(match<R"([\x41-\x5A]+)">(s));

  // ===== Email patterns =====
  folly::doNotOptimizeAway(match<R"(\w+@\w+\.\w+)">(s));
  folly::doNotOptimizeAway(
      test<R"([a-zA-Z0-9.]+@[a-zA-Z0-9]+\.[a-zA-Z]{2,4})">(s));

  // ===== Version / IP / Date patterns =====
  folly::doNotOptimizeAway(match<R"(\d+\.\d+\.\d+)">(s));
  folly::doNotOptimizeAway(match<R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})">(s));
  folly::doNotOptimizeAway(match<R"(\d{4}-\d{2}-\d{2})">(s));
  folly::doNotOptimizeAway(match<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})">(s));
  folly::doNotOptimizeAway(
      test<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[\w+\])">(s));
  folly::doNotOptimizeAway(
      test<R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})">(
          s));
  folly::doNotOptimizeAway(
      match<R"(top_rank\.dp([1-9][0-9]*)\.num_models)">(s));

  // ===== Backtracking-heavy / nested quantifiers =====
  folly::doNotOptimizeAway(match<"(a+)+b">(s));
  folly::doNotOptimizeAway(match<"(a*)*b">(s));
  folly::doNotOptimizeAway(match<"(x+x+)+y">(s));
  folly::doNotOptimizeAway(match<"(a|a)+b">(s));
  folly::doNotOptimizeAway(match<R"((\w+|\d+)+z)">(s));
  folly::doNotOptimizeAway(match<"(ab|a)+c">(s));
  folly::doNotOptimizeAway(match<R"((?:\w+|\d+)+z)">(s));
  folly::doNotOptimizeAway(match<R"((?:a*)*b)">(s));
  folly::doNotOptimizeAway(match<R"((?:a+)*b)">(s));
  folly::doNotOptimizeAway(match<R"((?:a*)+b)">(s));

  // ===== Disjoint patterns =====
  folly::doNotOptimizeAway(match<"[a-z]+[0-9]*[a-z]+">(s));
  folly::doNotOptimizeAway(match<"[a-z]+[0-9]+x">(s));
  folly::doNotOptimizeAway(match<R"(\w+\s+\w+)">(s));
  folly::doNotOptimizeAway(match<"[a-z]+[0-9]+">(s));

  // ===== Date parsing / structured bindings =====
  folly::doNotOptimizeAway(match<R"((\d{4})-(\d{2})-(\d{2}))">(s));
  folly::doNotOptimizeAway(match<R"((\w+)@(\w+))">(s));

  // ===== Additional patterns from tests =====
  folly::doNotOptimizeAway(match<R"([a-z]{3}\d{3})">(s));
  folly::doNotOptimizeAway(match<R"([a-z][a-z][a-z]\d)">(s));
  folly::doNotOptimizeAway(match<R"((\d+)\.(\d+))">(s));
  folly::doNotOptimizeAway(match<"[a-m]|[n-z]">(s));
  folly::doNotOptimizeAway(match<"a|[b-d]|e">(s));
  folly::doNotOptimizeAway(match<"^foo|^bar">(s));
  folly::doNotOptimizeAway(match<"foo$|bar$">(s));
  folly::doNotOptimizeAway(match<"a+a+b">(s));
  folly::doNotOptimizeAway(match<"a+b+c">(s));
  folly::doNotOptimizeAway(match<"(?:a|ab)+c">(s));
  folly::doNotOptimizeAway(match<"abc.*xyz">(s));
  folly::doNotOptimizeAway(match<"a{1}">(s));
  folly::doNotOptimizeAway(match<"ab{1}c">(s));

  return 0;
}
