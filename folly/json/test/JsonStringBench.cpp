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

#include <folly/json/json.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>

namespace {

constexpr unsigned kBatchSize = 64;

enum class Layout { Standalone, CompactArray, PrettyArray, ObjectKeys };
enum class Parser { Default, Options, Json5 };

struct Fixture {
  std::string input;
  folly::dynamic expected;
  unsigned strings;
};

std::string plainText(size_t length) {
  constexpr std::string_view text = "regular short text 0123456789";
  const auto repeats = length / text.size();
  const auto remainder = length % text.size();
  std::string result;
  result.reserve(length);
  for (size_t i = 0; i < repeats; ++i) {
    result.append(text);
  }
  result.append(text.substr(0, remainder));
  return result;
}

Fixture makeFixture(
    std::vector<std::string> const& values, Layout layout, char quote = '"') {
  if (layout == Layout::Standalone) {
    return {quote + values.front() + quote, values.front(), 1};
  }

  Fixture fixture{"[", folly::dynamic::array(), unsigned(values.size())};
  const bool pretty = layout == Layout::PrettyArray;
  for (auto const& value : values) {
    if (fixture.expected.size()) {
      fixture.input += ',';
    }
    if (pretty) {
      fixture.input += "\n  ";
    }
    if (layout == Layout::ObjectKeys) {
      fixture.input += '{';
    }
    fixture.input += quote;
    fixture.input += value;
    fixture.input += quote;
    if (layout == Layout::ObjectKeys) {
      fixture.input += ":0}";
      fixture.expected.push_back(folly::dynamic::object(value, 0));
    } else {
      fixture.expected.push_back(value);
    }
  }
  fixture.input += pretty ? "\n]" : "]";
  return fixture;
}

template <bool ExplicitOptions = false>
unsigned runFixture(
    folly::BenchmarkSuspender& suspend,
    unsigned iters,
    Fixture const& fixture,
    bool json5 = false) {
  folly::json::serialization_opts opts;
  opts.allow_json5_experimental = json5;
  const auto parse = [&] {
    if constexpr (ExplicitOptions) {
      return folly::parseJson(fixture.input, opts);
    } else {
      return folly::parseJson(fixture.input);
    }
  };
  if (parse() != fixture.expected) {
    throw std::logic_error("JSON string benchmark fixture mismatch");
  }
  return suspend.dismissing([&] {
    for (unsigned i = 0; i < iters; ++i) {
      folly::doNotOptimizeAway(parse());
    }
    return iters * fixture.strings;
  });
}

unsigned parsePlainString(
    unsigned iters,
    size_t length,
    Layout layout,
    Parser parser = Parser::Default) {
  folly::BenchmarkSuspender suspend;
  const auto count = layout == Layout::Standalone ? 1 : kBatchSize;
  const auto fixture = makeFixture(
      std::vector<std::string>(count, plainText(length)),
      layout,
      parser == Parser::Json5 ? '\'' : '"');
  return parser == Parser::Default
      ? runFixture(suspend, iters, fixture)
      : runFixture<true>(suspend, iters, fixture, parser == Parser::Json5);
}

unsigned parseMixedStrings(unsigned iters, bool pretty) {
  folly::BenchmarkSuspender suspend;
  // Synthetic short-heavy distribution, not a measured production histogram.
  constexpr std::array<std::pair<size_t, unsigned>, 8> distribution{{
      {0, 64},
      {1, 32},
      {2, 16},
      {4, 8},
      {8, 4},
      {16, 2},
      {64, 1},
      {256, 1},
  }};
  std::vector<std::string> values;
  for (auto [length, count] : distribution) {
    for (unsigned i = 0; i < count; ++i) {
      values.push_back(plainText(length));
    }
  }
  std::minstd_rand random(42);
  std::shuffle(values.begin(), values.end(), random);
  const auto fixture =
      makeFixture(values, pretty ? Layout::PrettyArray : Layout::CompactArray);
  return runFixture(suspend, iters, fixture);
}

unsigned parseStringControl(
    unsigned iters, std::string_view encoded, std::string_view decoded) {
  folly::BenchmarkSuspender suspend;
  auto fixture = makeFixture(
      std::vector<std::string>(kBatchSize, std::string(encoded)),
      Layout::CompactArray);
  fixture.expected = folly::dynamic::array();
  for (unsigned i = 0; i < kBatchSize; ++i) {
    fixture.expected.push_back(std::string(decoded));
  }
  return runFixture(suspend, iters, fixture);
}

unsigned parseSparseSpecial(
    unsigned iters,
    size_t length,
    size_t position,
    bool escaped,
    Parser parser = Parser::Default) {
  folly::BenchmarkSuspender suspend;
  auto body = plainText(length);
  body[position] = '\t';
  auto encoded = plainText(length);
  encoded.replace(position, 1, escaped ? "\\t" : "\t");
  Fixture fixture{'"' + encoded + '"', body, 1};
  return parser == Parser::Default
      ? runFixture(suspend, iters, fixture)
      : runFixture<true>(suspend, iters, fixture);
}

// Each reported iteration is one string/key, including its share of document
// parsing and destruction. Setup and fixture validation are not timed.
#define PLAIN_STRING_LENGTHS(label, layout, parser)                           \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_0, 0, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_1, 1, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_2, 2, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_3, 3, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_4, 4, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_5, 5, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_6, 6, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_7, 7, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_8, 8, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(parsePlainString, label##_9, 9, layout, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_15, 15, layout, parser)                       \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_16, 16, layout, parser)                       \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_17, 17, layout, parser)                       \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_22, 22, layout, parser)                       \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_23, 23, layout, parser)                       \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_24, 24, layout, parser)                       \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_32, 32, layout, parser)                       \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_64, 64, layout, parser)                       \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_256, 256, layout, parser)                     \
  BENCHMARK_NAMED_PARAM_MULTI(                                                \
      parsePlainString, label##_1024, 1024, layout, parser)

PLAIN_STRING_LENGTHS(Standalone, Layout::Standalone, Parser::Default)
PLAIN_STRING_LENGTHS(CompactArray, Layout::CompactArray, Parser::Default)
PLAIN_STRING_LENGTHS(PrettyArray, Layout::PrettyArray, Parser::Default)
PLAIN_STRING_LENGTHS(ObjectKeys, Layout::ObjectKeys, Parser::Default)
PLAIN_STRING_LENGTHS(OptionsStandalone, Layout::Standalone, Parser::Options)
PLAIN_STRING_LENGTHS(OptionsCompactArray, Layout::CompactArray, Parser::Options)
#undef PLAIN_STRING_LENGTHS

BENCHMARK_NAMED_PARAM_MULTI(parseMixedStrings, Compact, false)
BENCHMARK_NAMED_PARAM_MULTI(parseMixedStrings, Pretty, true)

BENCHMARK_NAMED_PARAM_MULTI(
    parseStringControl, EscapeFirst, R"(\ntext)", "\ntext")
BENCHMARK_NAMED_PARAM_MULTI(
    parseStringControl, EscapeAfterTwo, R"(ab\ntext)", "ab\ntext")
BENCHMARK_NAMED_PARAM_MULTI(
    parseStringControl, EscapeAfterEight, R"(abcdefgh\ntext)", "abcdefgh\ntext")
BENCHMARK_NAMED_PARAM_MULTI(
    parseStringControl, EscapeDense, R"(\n\t\"\\)", "\n\t\"\\")
BENCHMARK_NAMED_PARAM_MULTI(
    parseStringControl, Utf8, "caf\xc3\xa9", "caf\xc3\xa9")
BENCHMARK_NAMED_PARAM_MULTI(
    parseStringControl, RawNewlines, "ab\ncd\n\nef", "ab\ncd\n\nef")

BENCHMARK_NAMED_PARAM_MULTI(
    parsePlainString, Json5_0, 0, Layout::Standalone, Parser::Json5)
BENCHMARK_NAMED_PARAM_MULTI(
    parsePlainString, Json5_1, 1, Layout::Standalone, Parser::Json5)
BENCHMARK_NAMED_PARAM_MULTI(
    parsePlainString, Json5_2, 2, Layout::Standalone, Parser::Json5)
BENCHMARK_NAMED_PARAM_MULTI(
    parsePlainString, Json5_4, 4, Layout::Standalone, Parser::Json5)
BENCHMARK_NAMED_PARAM_MULTI(
    parsePlainString, Json5_8, 8, Layout::Standalone, Parser::Json5)
BENCHMARK_NAMED_PARAM_MULTI(
    parsePlainString, Json5_16, 16, Layout::Standalone, Parser::Json5)

#define SPARSE_SPECIAL_LENGTH(label, length, escaped, parser)                  \
  BENCHMARK_NAMED_PARAM_MULTI(                                                 \
      parseSparseSpecial, label##_first, length, 0, escaped, parser)           \
  BENCHMARK_NAMED_PARAM_MULTI(                                                 \
      parseSparseSpecial, label##_middle, length, length / 2, escaped, parser) \
  BENCHMARK_NAMED_PARAM_MULTI(                                                 \
      parseSparseSpecial, label##_last, length, length - 1, escaped, parser)

#define SPARSE_SPECIAL_LENGTHS(label, escaped, parser)       \
  SPARSE_SPECIAL_LENGTH(label##_64, 64, escaped, parser)     \
  SPARSE_SPECIAL_LENGTH(label##_1024, 1024, escaped, parser) \
  SPARSE_SPECIAL_LENGTH(label##_65536, 65536, escaped, parser)

SPARSE_SPECIAL_LENGTHS(Escape, true, Parser::Default)
SPARSE_SPECIAL_LENGTHS(RawControl, false, Parser::Default)
SPARSE_SPECIAL_LENGTHS(OptionsEscape, true, Parser::Options)
SPARSE_SPECIAL_LENGTHS(OptionsRawControl, false, Parser::Options)
#undef SPARSE_SPECIAL_LENGTHS
#undef SPARSE_SPECIAL_LENGTH

} // namespace

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
}
