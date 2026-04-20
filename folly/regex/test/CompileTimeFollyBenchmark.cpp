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
  folly::doNotOptimizeAway(test<"hello">(s));
  folly::doNotOptimizeAway(test<"abc">(s));
  folly::doNotOptimizeAway(test<"world">(s));

  // ===== Dot / any char =====
  folly::doNotOptimizeAway(test<"a.b">(s));
  folly::doNotOptimizeAway(test<"a.*b">(s));
  folly::doNotOptimizeAway(test<"a.*?b">(s));
  folly::doNotOptimizeAway(test<".*x">(s));

  // ===== Character classes =====
  folly::doNotOptimizeAway(test<"[abc]">(s));
  folly::doNotOptimizeAway(test<"[a-z]+">(s));
  folly::doNotOptimizeAway(test<"[^0-9]+">(s));
  folly::doNotOptimizeAway(test<"[a-zA-Z0-9]+">(s));
  folly::doNotOptimizeAway(test<"[A-Z_0-9]+">(s));
  folly::doNotOptimizeAway(test<"[-a]">(s));
  folly::doNotOptimizeAway(test<"[^abc]">(s));
  folly::doNotOptimizeAway(test<"[^a-z]">(s));
  folly::doNotOptimizeAway(test<"[^a-z]+">(s));
  folly::doNotOptimizeAway(test<"[a-z ,.]+">(s));
  folly::doNotOptimizeAway(test<R"([a-zA-Z0-9_.\-]*(/[a-zA-Z0-9_.\-]+)*)">(s));
  folly::doNotOptimizeAway(test<R"([a-zA-Z0-9_.:/\-|]+)">(s));

  // ===== Shorthand character classes =====
  folly::doNotOptimizeAway(test<R"(\d+)">(s));
  folly::doNotOptimizeAway(test<R"(\w+)">(s));
  folly::doNotOptimizeAway(test<R"(\s+)">(s));
  folly::doNotOptimizeAway(test<R"(\D+)">(s));
  folly::doNotOptimizeAway(test<R"(\W+)">(s));
  folly::doNotOptimizeAway(test<R"(\S+)">(s));

  // ===== Quantifiers =====
  folly::doNotOptimizeAway(test<"ab*c">(s));
  folly::doNotOptimizeAway(test<"ab+c">(s));
  folly::doNotOptimizeAway(test<"ab?c">(s));
  folly::doNotOptimizeAway(test<"a{2,4}">(s));
  folly::doNotOptimizeAway(test<"a{3}">(s));
  folly::doNotOptimizeAway(test<"a{2,}">(s));
  folly::doNotOptimizeAway(test<"a*">(s));

  // ===== Lazy quantifiers =====
  folly::doNotOptimizeAway(test<"[a-z]+?">(s));
  folly::doNotOptimizeAway(test<"a??">(s));
  folly::doNotOptimizeAway(test<"a{2,5}?">(s));
  folly::doNotOptimizeAway(test<R"([a-z]+?\d)">(s));
  folly::doNotOptimizeAway(test<"a+?a">(s));

  // ===== Alternation =====
  folly::doNotOptimizeAway(test<"cat|dog">(s));
  folly::doNotOptimizeAway(test<"quick|slow|fast|lazy">(s));
  folly::doNotOptimizeAway(test<"cat|dog|bird">(s));
  folly::doNotOptimizeAway(
      test<"error|warning|info|debug|trace|fatal|critical">(s));
  folly::doNotOptimizeAway(test<"GET|POST|PUT|DELETE|PATCH|HEAD|OPTIONS">(s));
  folly::doNotOptimizeAway(
      test<"alpha|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet">(
          s));
  folly::doNotOptimizeAway(
      test<"(dev|devvm|devrs|devbig|devgpu|shellserver)">(s));
  folly::doNotOptimizeAway(test<"a|b|c|d">(s));
  folly::doNotOptimizeAway(test<"abc|def|ghi">(s));
  folly::doNotOptimizeAway(test<"foobar|foobaz|fooqux">(s));
  folly::doNotOptimizeAway(test<"dev|devvm|devrs">(s));

  // ===== Groups =====
  folly::doNotOptimizeAway(test<R"((\d+))">(s));
  folly::doNotOptimizeAway(test<R"((\d+)-(\w+))">(s));
  folly::doNotOptimizeAway(test<R"(((\d+)-(\w+)))">(s));
  folly::doNotOptimizeAway(test<"(?:ab)+">(s));
  folly::doNotOptimizeAway(test<R"((\d+)\.(\d+)\.(\d+))">(s));
  folly::doNotOptimizeAway(test<R"((\w+)@(\w+)\.(\w+))">(s));
  folly::doNotOptimizeAway(test<R"(_ct(\d+))">(s));
  folly::doNotOptimizeAway(test<R"(_s(\d+))">(s));
  folly::doNotOptimizeAway(test<R"((.+)-fg-(\d+))">(s));
  folly::doNotOptimizeAway(test<R"((.+)-bg-(\d+))">(s));
  folly::doNotOptimizeAway(test<"(?:(?:a))">(s));
  folly::doNotOptimizeAway(test<"(?:(?:(?:abc)))">(s));
  folly::doNotOptimizeAway(test<"(a)(b)(c)">(s));
  folly::doNotOptimizeAway(test<"(a)(?:b)(c)">(s));
  folly::doNotOptimizeAway(test<"((a)(b))">(s));

  // ===== Anchors =====
  folly::doNotOptimizeAway(test<"^hello$">(s));
  folly::doNotOptimizeAway(test<"^hello">(s));
  folly::doNotOptimizeAway(test<"^start$">(s));

  // ===== Escape sequences =====
  folly::doNotOptimizeAway(test<R"(a\.b)">(s));
  folly::doNotOptimizeAway(test<R"(a\tb)">(s));
  folly::doNotOptimizeAway(test<R"(\(\)\[\])">(s));
  folly::doNotOptimizeAway(test<R"(\x41)">(s));
  folly::doNotOptimizeAway(test<R"([\x41-\x5A]+)">(s));

  // ===== Email patterns =====
  folly::doNotOptimizeAway(test<R"(\w+@\w+\.\w+)">(s));
  folly::doNotOptimizeAway(
      test<R"([a-zA-Z0-9.]+@[a-zA-Z0-9]+\.[a-zA-Z]{2,4})">(s));

  // ===== Version / IP / Date patterns =====
  folly::doNotOptimizeAway(test<R"(\d+\.\d+\.\d+)">(s));
  folly::doNotOptimizeAway(test<R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})">(s));
  folly::doNotOptimizeAway(test<R"(\d{4}-\d{2}-\d{2})">(s));
  folly::doNotOptimizeAway(test<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})">(s));
  folly::doNotOptimizeAway(
      test<R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \[\w+\])">(s));
  folly::doNotOptimizeAway(
      test<R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})">(
          s));
  folly::doNotOptimizeAway(test<R"(top_rank\.dp([1-9][0-9]*)\.num_models)">(s));

  // ===== Backtracking-heavy / nested quantifiers =====
  folly::doNotOptimizeAway(test<"(a+)+b">(s));
  folly::doNotOptimizeAway(test<"(a*)*b">(s));
  folly::doNotOptimizeAway(test<"(x+x+)+y">(s));
  folly::doNotOptimizeAway(test<"(a|a)+b">(s));
  folly::doNotOptimizeAway(test<R"((\w+|\d+)+z)">(s));
  folly::doNotOptimizeAway(test<"(ab|a)+c">(s));
  folly::doNotOptimizeAway(test<R"((?:\w+|\d+)+z)">(s));
  folly::doNotOptimizeAway(test<R"((?:a*)*b)">(s));
  folly::doNotOptimizeAway(test<R"((?:a+)*b)">(s));
  folly::doNotOptimizeAway(test<R"((?:a*)+b)">(s));

  // ===== Disjoint patterns =====
  folly::doNotOptimizeAway(test<"[a-z]+[0-9]*[a-z]+">(s));
  folly::doNotOptimizeAway(test<"[a-z]+[0-9]+x">(s));
  folly::doNotOptimizeAway(test<R"(\w+\s+\w+)">(s));
  folly::doNotOptimizeAway(test<"[a-z]+[0-9]+">(s));

  // ===== Date parsing / structured bindings =====
  folly::doNotOptimizeAway(test<R"((\d{4})-(\d{2})-(\d{2}))">(s));
  folly::doNotOptimizeAway(test<R"((\w+)@(\w+))">(s));

  // ===== Additional patterns from tests =====
  folly::doNotOptimizeAway(test<R"([a-z]{3}\d{3})">(s));
  folly::doNotOptimizeAway(test<R"([a-z][a-z][a-z]\d)">(s));
  folly::doNotOptimizeAway(test<R"((\d+)\.(\d+))">(s));
  folly::doNotOptimizeAway(test<"[a-m]|[n-z]">(s));
  folly::doNotOptimizeAway(test<"a|[b-d]|e">(s));
  folly::doNotOptimizeAway(test<"^foo|^bar">(s));
  folly::doNotOptimizeAway(test<"foo$|bar$">(s));
  folly::doNotOptimizeAway(test<"a+a+b">(s));
  folly::doNotOptimizeAway(test<"a+b+c">(s));
  folly::doNotOptimizeAway(test<"(?:a|ab)+c">(s));
  folly::doNotOptimizeAway(test<"abc.*xyz">(s));
  folly::doNotOptimizeAway(test<"a{1}">(s));
  folly::doNotOptimizeAway(test<"ab{1}c">(s));

  return 0;
}
