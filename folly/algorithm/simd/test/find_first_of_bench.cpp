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

#include <folly/algorithm/simd/find_first_of.h>
#include <folly/algorithm/simd/find_first_of_extra.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <folly/lang/Keep.h>

using namespace std::literals;

static constexpr auto json_str_bound = "\\\""sv;
static constexpr auto json_ws_whitespace = " \r\n\t"sv;

//  generic find_first_of

extern "C" FOLLY_KEEP size_t
check_folly_simd_stdfind_scalar_finder_find_first_of(
    folly::simd::stdfind_scalar_finder_first_of const& needle,
    std::string_view const haystack) {
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_default_scalar_finder_find_first_of(
    folly::simd::default_scalar_finder_first_of const& needle,
    std::string_view const haystack) {
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_ltindex_scalar_finder_find_first_of(
    folly::simd::ltindex_scalar_finder_first_of const& needle,
    std::string_view const haystack) {
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_ltsparse_scalar_finder_find_first_of(
    folly::simd::ltsparse_scalar_finder_first_of const& needle,
    std::string_view const haystack) {
  return needle(haystack);
}

#if __cpp_lib_execution >= 201902L
extern "C" FOLLY_KEEP size_t
check_folly_simd_stdfind_vector_finder_find_first_of(
    folly::simd::stdfind_vector_finder_first_of const& needle,
    std::string_view const haystack) {
  return needle(haystack);
}
#endif

extern "C" FOLLY_KEEP size_t
check_folly_simd_default_vector_stdfind_scalar_finder_find_first_of(
    folly::simd::default_vector_finder_first_of const& vector,
    folly::simd::stdfind_scalar_finder_first_of const& scalar,
    std::string_view const haystack) {
  return vector(scalar, haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_shuffle_vector_stdfind_scalar_finder_find_first_of(
    folly::simd::shuffle_vector_finder_first_of const& vector,
    folly::simd::stdfind_scalar_finder_first_of const& scalar,
    std::string_view const haystack) {
  return vector(scalar, haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_azmatch_vector_stdfind_scalar_finder_find_first_of(
    folly::simd::azmatch_vector_finder_first_of const& vector,
    folly::simd::stdfind_scalar_finder_first_of const& scalar,
    std::string_view const haystack) {
  return vector(scalar, haystack);
}

//  generic find_first_not_of

extern "C" FOLLY_KEEP size_t
check_folly_simd_default_scalar_finder_find_first_not_of(
    folly::simd::default_scalar_finder_first_not_of const& needle,
    std::string_view const haystack) {
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_ltindex_scalar_finder_find_first_not_of(
    folly::simd::ltindex_scalar_finder_first_not_of const& needle,
    std::string_view const haystack) {
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_ltsparse_scalar_finder_find_first_not_of(
    folly::simd::ltsparse_scalar_finder_first_not_of const& needle,
    std::string_view const haystack) {
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_default_vector_default_scalar_finder_find_first_not_of(
    folly::simd::default_vector_finder_first_not_of const& vector,
    folly::simd::default_scalar_finder_first_not_of const& scalar,
    std::string_view const haystack) {
  return vector(scalar, haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_shuffle_vector_default_scalar_finder_find_first_not_of(
    folly::simd::shuffle_vector_finder_first_not_of const& vector,
    folly::simd::default_scalar_finder_first_not_of const& scalar,
    std::string_view const haystack) {
  return vector(scalar, haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_azmatch_vector_default_scalar_finder_find_first_not_of(
    folly::simd::azmatch_vector_finder_first_not_of const& vector,
    folly::simd::default_scalar_finder_first_not_of const& scalar,
    std::string_view const haystack) {
  return vector(scalar, haystack);
}

//  specialized find_first_of

extern "C" FOLLY_KEEP size_t
check_folly_simd_stdfind_scalar_finder_find_first_of_json_str(
    std::string_view const haystack) {
  using scalar = folly::simd::stdfind_scalar_finder_first_of;
  static constexpr auto needle = scalar(json_str_bound);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_default_scalar_finder_find_first_of_json_str(
    std::string_view const haystack) {
  using scalar = folly::simd::default_scalar_finder_first_of;
  static constexpr auto needle = scalar(json_str_bound);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_ltindex_scalar_finder_find_first_of_json_str(
    std::string_view const haystack) {
  using scalar = folly::simd::ltindex_scalar_finder_first_of;
  static constexpr auto needle = scalar(json_str_bound);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_ltsparse_scalar_finder_find_first_of_json_str(
    std::string_view const haystack) {
  using scalar = folly::simd::ltsparse_scalar_finder_first_of;
  static constexpr auto needle = scalar(json_str_bound);
  return needle(haystack);
}

#if __cpp_lib_execution >= 201902L
extern "C" FOLLY_KEEP size_t
check_folly_simd_stdfind_vector_finder_find_first_of_json_str(
    std::string_view const haystack) {
  using vector = folly::simd::stdfind_vector_finder_first_of;
  static constexpr auto needle = vector(json_str_bound);
  return needle(haystack);
}
#endif

extern "C" FOLLY_KEEP size_t
check_folly_simd_default_vector_stdfind_scalar_finder_find_first_of_json_str(
    std::string_view const haystack) {
  using scalar = folly::simd::stdfind_scalar_finder_first_of;
  using vector = folly::simd::default_vector_finder_first_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  static constexpr auto needle = finder(json_str_bound);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_shuffle_vector_stdfind_scalar_finder_find_first_of_json_str(
    std::string_view const haystack) {
  using scalar = folly::simd::stdfind_scalar_finder_first_of;
  using vector = folly::simd::shuffle_vector_finder_first_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  static constexpr auto needle = finder(json_str_bound);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_azmatch_vector_stdfind_scalar_finder_find_first_of_json_str(
    std::string_view const haystack) {
  using scalar = folly::simd::stdfind_scalar_finder_first_of;
  using vector = folly::simd::azmatch_vector_finder_first_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  static constexpr auto needle = finder(json_str_bound);
  return needle(haystack);
}

//  specialized find_first_not_of

extern "C" FOLLY_KEEP size_t
check_folly_simd_default_scalar_finder_find_first_not_of_json_ws(
    std::string_view const haystack) {
  using scalar = folly::simd::default_scalar_finder_first_not_of;
  static constexpr auto needle = scalar(json_ws_whitespace);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_ltindex_scalar_finder_find_first_not_of_json_ws(
    std::string_view const haystack) {
  using scalar = folly::simd::ltindex_scalar_finder_first_not_of;
  static constexpr auto needle = scalar(json_ws_whitespace);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_ltsparse_scalar_finder_find_first_not_of_json_ws(
    std::string_view const haystack) {
  using scalar = folly::simd::ltsparse_scalar_finder_first_not_of;
  static constexpr auto needle = scalar(json_ws_whitespace);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_default_vector_default_scalar_finder_find_first_not_of_json_ws(
    std::string_view const haystack) {
  using scalar = folly::simd::default_scalar_finder_first_not_of;
  using vector = folly::simd::default_vector_finder_first_not_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  static constexpr auto needle = finder(json_ws_whitespace);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_shuffle_vector_default_scalar_finder_find_first_not_of_json_ws(
    std::string_view const haystack) {
  using scalar = folly::simd::default_scalar_finder_first_not_of;
  using vector = folly::simd::shuffle_vector_finder_first_not_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  static constexpr auto needle = finder(json_ws_whitespace);
  return needle(haystack);
}

extern "C" FOLLY_KEEP size_t
check_folly_simd_azmatch_vector_default_scalar_finder_find_first_not_of_json_ws(
    std::string_view const haystack) {
  using scalar = folly::simd::default_scalar_finder_first_not_of;
  using vector = folly::simd::azmatch_vector_finder_first_not_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  static constexpr auto needle = finder(json_ws_whitespace);
  return needle(haystack);
}

static constexpr auto json_str_text = "abcdefghijklmopqrstuvwxyz"sv;

static std::mt19937 rng;

static std::string make_string(std::string_view alphabet, size_t run) {
  std::uniform_int_distribution<size_t> dist{0, alphabet.size() - 1};
  std::string ret;
  while (run--) {
    ret.push_back(alphabet[dist(rng)]);
  }
  return ret;
}

template <typename Finder>
FOLLY_NOINLINE static void do_find_first_op_of(
    size_t iters, Finder const& needle, std::string_view haystack) {
  size_t n = 0;
  while (iters--) {
    auto r = needle(haystack);
    folly::compiler_must_not_predict(r);
    n += r;
  }
  folly::compiler_must_not_elide(n);
}

template <typename Finder>
static void find_first_of_json_str(size_t iters, size_t run) {
  static constexpr Finder needle{json_str_bound};
  folly::BenchmarkSuspender bs;
  auto const s = ""s //
      + make_string(json_str_text, run) //
      + make_string(json_str_bound, 1) //
      + make_string(json_str_text, 47) //
      ;
  bs.dismissing([&] { //
    do_find_first_op_of(iters, needle, s);
  });
}

template <typename Finder>
static void find_first_not_of_json_ws(size_t iters, size_t run) {
  static constexpr Finder needle{json_ws_whitespace};
  folly::BenchmarkSuspender bs;
  auto const s = ""s //
      + make_string(json_ws_whitespace, run) //
      + make_string(json_str_text, 48) //
      ;
  bs.dismissing([&] { //
    do_find_first_op_of(iters, needle, s);
  });
}

void find_first_of_json_str_stdfind_scalar(size_t iters, size_t run) {
  using scalar = folly::simd::stdfind_scalar_finder_first_of;
  find_first_of_json_str<scalar>(iters, run);
}

void find_first_of_json_str_default_scalar(size_t iters, size_t run) {
  using scalar = folly::simd::default_scalar_finder_first_of;
  find_first_of_json_str<scalar>(iters, run);
}

void find_first_of_json_str_ltindex_scalar(size_t iters, size_t run) {
  using scalar = folly::simd::ltindex_scalar_finder_first_of;
  find_first_of_json_str<scalar>(iters, run);
}

void find_first_of_json_str_ltsparse_scalar(size_t iters, size_t run) {
  using scalar = folly::simd::ltindex_scalar_finder_first_of;
  find_first_of_json_str<scalar>(iters, run);
}

#if __cpp_lib_execution >= 201902L
void find_first_of_json_str_stdfind_vector(size_t iters, size_t run) {
  using vector = folly::simd::stdfind_vector_finder_first_of;
  find_first_of_json_str<vector>(iters, run);
}
#endif

void find_first_of_json_str_rngfind_vector(size_t iters, size_t run) {
  using vector = folly::simd::rngfind_vector_finder_first_of;
  find_first_of_json_str<vector>(iters, run);
}

void find_first_of_json_str_default_vector(size_t iters, size_t run) {
  using scalar = folly::simd::default_scalar_finder_first_of;
  using vector = folly::simd::default_vector_finder_first_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  find_first_of_json_str<finder>(iters, run);
}

void find_first_of_json_str_shuffle_vector(size_t iters, size_t run) {
  using scalar = folly::simd::default_scalar_finder_first_of;
  using vector = folly::simd::shuffle_vector_finder_first_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  find_first_of_json_str<finder>(iters, run);
}

void find_first_of_json_str_azmatch_vector(size_t iters, size_t run) {
  using scalar = folly::simd::default_scalar_finder_first_of;
  using vector = folly::simd::azmatch_vector_finder_first_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  find_first_of_json_str<finder>(iters, run);
}

void find_first_not_of_json_ws_default_scalar(size_t iters, size_t run) {
  using scalar = folly::simd::default_scalar_finder_first_not_of;
  find_first_not_of_json_ws<scalar>(iters, run);
}

void find_first_not_of_json_ws_ltindex_scalar(size_t iters, size_t run) {
  using scalar = folly::simd::ltindex_scalar_finder_first_not_of;
  find_first_not_of_json_ws<scalar>(iters, run);
}

void find_first_not_of_json_ws_ltsparse_scalar(size_t iters, size_t run) {
  using scalar = folly::simd::ltsparse_scalar_finder_first_not_of;
  find_first_not_of_json_ws<scalar>(iters, run);
}

void find_first_not_of_json_ws_default_vector(size_t iters, size_t run) {
  using scalar = folly::simd::default_scalar_finder_first_not_of;
  using vector = folly::simd::default_vector_finder_first_not_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  find_first_not_of_json_ws<finder>(iters, run);
}

void find_first_not_of_json_ws_shuffle_vector(size_t iters, size_t run) {
  using scalar = folly::simd::default_scalar_finder_first_not_of;
  using vector = folly::simd::shuffle_vector_finder_first_not_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  find_first_not_of_json_ws<finder>(iters, run);
}

void find_first_not_of_json_ws_azmatch_vector(size_t iters, size_t run) {
  using scalar = folly::simd::default_scalar_finder_first_not_of;
  using vector = folly::simd::azmatch_vector_finder_first_not_of;
  using finder = folly::simd::composite_finder_first_of<vector, scalar>;
  find_first_not_of_json_ws<finder>(iters, run);
}

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x00)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x01)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x02)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x03)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x04)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x05)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x06)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x07)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x08)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x09)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x0a)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x0b)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x0c)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x0d)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x0e)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x0f)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x10)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x11)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x12)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_scalar, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x00)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x01)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x02)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x03)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x04)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x05)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x06)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x07)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x08)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x09)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x0a)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x0b)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x0c)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x0d)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x0e)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x0f)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x10)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x11)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x12)
BENCHMARK_PARAM(find_first_of_json_str_default_scalar, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x00)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x01)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x02)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x03)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x04)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x05)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x06)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x07)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x08)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x09)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x0a)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x0b)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x0c)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x0d)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x0e)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x0f)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x10)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x11)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x12)
BENCHMARK_PARAM(find_first_of_json_str_ltindex_scalar, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x00)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x01)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x02)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x03)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x04)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x05)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x06)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x07)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x08)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x09)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x0a)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x0b)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x0c)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x0d)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x0e)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x0f)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x10)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x11)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x12)
BENCHMARK_PARAM(find_first_of_json_str_ltsparse_scalar, 0x13)

#if __cpp_lib_execution >= 201902L

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x00)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x01)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x02)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x03)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x04)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x05)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x06)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x07)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x08)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x09)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x0a)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x0b)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x0c)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x0d)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x0e)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x0f)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x10)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x11)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x12)
BENCHMARK_PARAM(find_first_of_json_str_stdfind_vector, 0x13)

#endif

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x00)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x01)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x02)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x03)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x04)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x05)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x06)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x07)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x08)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x09)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x0a)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x0b)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x0c)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x0d)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x0e)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x0f)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x10)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x11)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x12)
BENCHMARK_PARAM(find_first_of_json_str_rngfind_vector, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x00)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x01)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x02)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x03)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x04)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x05)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x06)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x07)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x08)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x09)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x0a)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x0b)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x0c)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x0d)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x0e)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x0f)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x10)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x11)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x12)
BENCHMARK_PARAM(find_first_of_json_str_default_vector, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x00)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x01)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x02)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x03)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x04)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x05)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x06)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x07)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x08)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x09)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x0a)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x0b)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x0c)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x0d)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x0e)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x0f)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x10)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x11)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x12)
BENCHMARK_PARAM(find_first_of_json_str_shuffle_vector, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x00)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x01)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x02)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x03)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x04)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x05)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x06)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x07)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x08)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x09)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x0a)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x0b)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x0c)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x0d)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x0e)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x0f)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x10)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x11)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x12)
BENCHMARK_PARAM(find_first_of_json_str_azmatch_vector, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x00)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x01)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x02)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x03)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x04)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x05)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x06)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x07)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x08)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x09)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x0a)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x0b)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x0c)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x0d)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x0e)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x0f)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x10)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x11)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x12)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_scalar, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x00)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x01)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x02)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x03)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x04)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x05)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x06)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x07)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x08)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x09)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x0a)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x0b)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x0c)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x0d)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x0e)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x0f)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x10)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x11)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x12)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltindex_scalar, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x00)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x01)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x02)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x03)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x04)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x05)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x06)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x07)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x08)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x09)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x0a)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x0b)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x0c)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x0d)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x0e)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x0f)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x10)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x11)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x12)
BENCHMARK_PARAM(find_first_not_of_json_ws_ltsparse_scalar, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x00)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x01)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x02)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x03)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x04)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x05)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x06)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x07)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x08)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x09)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x0a)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x0b)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x0c)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x0d)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x0e)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x0f)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x10)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x11)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x12)
BENCHMARK_PARAM(find_first_not_of_json_ws_default_vector, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x00)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x01)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x02)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x03)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x04)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x05)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x06)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x07)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x08)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x0b)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x0c)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x0d)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x0e)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x0f)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x10)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x11)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x12)
BENCHMARK_PARAM(find_first_not_of_json_ws_shuffle_vector, 0x13)

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x00)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x01)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x02)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x03)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x04)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x05)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x06)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x07)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x08)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x0b)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x0c)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x0d)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x0e)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x0f)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x10)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x11)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x12)
BENCHMARK_PARAM(find_first_not_of_json_ws_azmatch_vector, 0x13)

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
