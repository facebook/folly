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

#include <folly/lang/ToAscii.h>

#define __STDC_FORMAT_MACROS

#include <cinttypes>

#include <fmt/format.h>

#include <folly/Benchmark.h>
#include <folly/lang/Keep.h>
#include <folly/portability/FmtCompile.h>

#if __has_include(<charconv>)
#include <charconv>
#endif

using abc = folly::to_ascii_alphabet_lower;

#define FOLLY_TO_ASCII_BENCH_CHECK_SIZE(impl, base)                 \
  extern "C" FOLLY_KEEP size_t check_to_ascii_size_##impl##_##base( \
      uint64_t v) {                                                 \
    return folly::detail::to_ascii_size_##impl<base>(v);            \
  }
#define FOLLY_TO_ASCII_BENCH_CHECK_WITH(impl, base)                        \
  extern "C" FOLLY_KEEP void check_bound_to_ascii_with_##impl##_##base(    \
      char* buf, size_t size, uint64_t v) {                                \
    folly::detail::to_ascii_with_##impl<base, abc>(buf, size, v);          \
  }                                                                        \
  extern "C" FOLLY_KEEP size_t check_return_to_ascii_with_##impl##_##base( \
      char* buf, uint64_t v) {                                             \
    auto const size = folly::to_ascii_size<base>(v);                       \
    folly::detail::to_ascii_with_##impl<base, abc>(buf, size, v);          \
    return size;                                                           \
  }
#define FOLLY_TO_ASCII_BENCH_CHECK(base)                                    \
  extern "C" FOLLY_KEEP size_t check_to_ascii_range_##base(                 \
      char* outb, char* oute, uint64_t v) {                                 \
    return folly::detail::to_ascii_with_route<base, abc>(outb, oute, v);    \
  }                                                                         \
  extern "C" FOLLY_KEEP size_t check_to_ascii_array_##base(                 \
      char(&out)[(folly::to_ascii_size_max<base, uint64_t>)], uint64_t v) { \
    return folly::detail::to_ascii_with_route<base, abc>(out, v);           \
  }
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(imuls, 16)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(imuls, 10)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(imuls, 8)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(idivs, 16)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(idivs, 10)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(idivs, 8)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(array, 16)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(array, 10)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(array, 8)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(clzll, 16)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(clzll, 10)
FOLLY_TO_ASCII_BENCH_CHECK_SIZE(clzll, 8)
FOLLY_TO_ASCII_BENCH_CHECK_WITH(basic, 16)
FOLLY_TO_ASCII_BENCH_CHECK_WITH(basic, 10)
FOLLY_TO_ASCII_BENCH_CHECK_WITH(basic, 8)
FOLLY_TO_ASCII_BENCH_CHECK_WITH(array, 16)
FOLLY_TO_ASCII_BENCH_CHECK_WITH(array, 10)
FOLLY_TO_ASCII_BENCH_CHECK_WITH(array, 8)
FOLLY_TO_ASCII_BENCH_CHECK_WITH(table, 16)
FOLLY_TO_ASCII_BENCH_CHECK_WITH(table, 10)
FOLLY_TO_ASCII_BENCH_CHECK_WITH(table, 8)
FOLLY_TO_ASCII_BENCH_CHECK(16)
FOLLY_TO_ASCII_BENCH_CHECK(10)
FOLLY_TO_ASCII_BENCH_CHECK(8)

namespace {

template <uint64_t Base>
struct inputs;

template <>
struct inputs<10> {
  static constexpr uint64_t const data[21] = {
      0,
      1,
      12,
      123,
      1234,
      12345,
      123456,
      1234567,
      12345678,
      123456789,
      1234567890,
      12345678901,
      123456789012,
      1234567890123,
      12345678901234,
      123456789012345,
      1234567890123456,
      12345678901234567,
      123456789012345678,
      1234567890123456789,
      12345678901234567890u,
  };
};
constexpr uint64_t const inputs<10>::data[21];

template <>
struct inputs<16> {
  static constexpr uint64_t const data[17] = {
      0x0,
      0x1,
      0x12,
      0x123,
      0x1234,
      0x12345,
      0x123456,
      0x1234567,
      0x12345678,
      0x123456789,
      0x123456789a,
      0x123456789ab,
      0x123456789abc,
      0x123456789abcd,
      0x123456789abcde,
      0x123456789abcdef,
      0x123456789abcdef0,
  };
};
constexpr uint64_t const inputs<16>::data[17];

} // namespace

static void fill_zero(char* out, size_t size, size_t index) {
  (void)index;
  std::memset(out, 0, size);
}

template <typename... A>
static void call_to_chars(FOLLY_MAYBE_UNUSED A... a) {
#if __cpp_lib_to_chars >= 201611L
  std::to_chars(a...);
#endif
}

template <uint64_t Base, typename F>
static void to_ascii_size_go(size_t iters, size_t index, F f) {
  while (iters--) {
    auto const v = inputs<Base>::data[index] + (iters % 8);
    auto const size = [&]() FOLLY_NOINLINE { return f(v); }();
    folly::doNotOptimizeAway(size);
  }
}

template <uint64_t Base, typename F>
static void to_ascii_go(size_t iters, size_t index, F f) {
  char out[1 + folly::to_ascii_size_max<Base, uint64_t>];
  while (iters--) {
    auto const v = inputs<Base>::data[index] + (iters % 8);
    auto const size = folly::to_ascii_size<Base>(v);
    [&]() FOLLY_NOINLINE { f(&out[0], size, v); }();
  }
  folly::doNotOptimizeAway(out);
}

//  to_ascii_size<10>

static void fmt_formatted_size_10(size_t iters, size_t index) {
  to_ascii_size_go<10>(iters, index, [](auto v) {
    return fmt::formatted_size(FOLLY_FMT_COMPILE("{}"), v);
  });
}

static void to_ascii_size_imuls_10(size_t iters, size_t index) {
  to_ascii_size_go<10>(iters, index, [](auto v) {
    return folly::detail::to_ascii_size_imuls<10>(v);
  });
}

static void to_ascii_size_idivs_10(size_t iters, size_t index) {
  to_ascii_size_go<10>(iters, index, [](auto v) {
    return folly::detail::to_ascii_size_idivs<10>(v);
  });
}

static void to_ascii_size_array_10(size_t iters, size_t index) {
  to_ascii_size_go<10>(iters, index, [](auto v) {
    return folly::detail::to_ascii_size_array<10>(v);
  });
}

static void to_ascii_size_clzll_10(size_t iters, size_t index) {
  to_ascii_size_go<10>(iters, index, [](auto v) {
    return folly::detail::to_ascii_size_clzll<10>(v);
  });
}

#define BENCHMARK_GROUP(index)                   \
  BENCHMARK_DRAW_LINE();                         \
  BENCHMARK_PARAM(fmt_formatted_size_10, index)  \
  BENCHMARK_PARAM(to_ascii_size_imuls_10, index) \
  BENCHMARK_PARAM(to_ascii_size_idivs_10, index) \
  BENCHMARK_PARAM(to_ascii_size_array_10, index) \
  BENCHMARK_PARAM(to_ascii_size_clzll_10, index)

BENCHMARK_GROUP(1)
BENCHMARK_GROUP(2)
BENCHMARK_GROUP(3)
BENCHMARK_GROUP(4)
BENCHMARK_GROUP(5)
BENCHMARK_GROUP(6)
BENCHMARK_GROUP(7)
BENCHMARK_GROUP(8)
BENCHMARK_GROUP(9)
BENCHMARK_GROUP(10)
BENCHMARK_GROUP(11)
BENCHMARK_GROUP(12)
BENCHMARK_GROUP(13)
BENCHMARK_GROUP(14)
BENCHMARK_GROUP(15)
BENCHMARK_GROUP(16)
BENCHMARK_GROUP(17)
BENCHMARK_GROUP(18)
BENCHMARK_GROUP(19)
BENCHMARK_GROUP(20)

#undef BENCHMARK_GROUP

BENCHMARK_DRAW_LINE();

//  to_ascii_size<16>

static void fmt_formatted_size_16(size_t iters, size_t index) {
  to_ascii_size_go<16>(iters, index, [](auto v) {
    return fmt::formatted_size(FOLLY_FMT_COMPILE("{:x}"), v);
  });
}

static void to_ascii_size_imuls_16(size_t iters, size_t index) {
  to_ascii_size_go<16>(iters, index, [](auto v) {
    return folly::detail::to_ascii_size_imuls<16>(v);
  });
}

static void to_ascii_size_idivs_16(size_t iters, size_t index) {
  to_ascii_size_go<16>(iters, index, [](auto v) {
    return folly::detail::to_ascii_size_idivs<16>(v);
  });
}

static void to_ascii_size_array_16(size_t iters, size_t index) {
  to_ascii_size_go<16>(iters, index, [](auto v) {
    return folly::detail::to_ascii_size_array<16>(v);
  });
}

static void to_ascii_size_clzll_16(size_t iters, size_t index) {
  to_ascii_size_go<16>(iters, index, [](auto v) {
    return folly::detail::to_ascii_size_clzll<16>(v);
  });
}

#define BENCHMARK_GROUP(index)                   \
  BENCHMARK_DRAW_LINE();                         \
  BENCHMARK_PARAM(fmt_formatted_size_16, index)  \
  BENCHMARK_PARAM(to_ascii_size_imuls_16, index) \
  BENCHMARK_PARAM(to_ascii_size_idivs_16, index) \
  BENCHMARK_PARAM(to_ascii_size_array_16, index) \
  BENCHMARK_PARAM(to_ascii_size_clzll_16, index)

BENCHMARK_GROUP(1)
BENCHMARK_GROUP(2)
BENCHMARK_GROUP(3)
BENCHMARK_GROUP(4)
BENCHMARK_GROUP(5)
BENCHMARK_GROUP(6)
BENCHMARK_GROUP(7)
BENCHMARK_GROUP(8)
BENCHMARK_GROUP(9)
BENCHMARK_GROUP(10)
BENCHMARK_GROUP(11)
BENCHMARK_GROUP(12)
BENCHMARK_GROUP(13)
BENCHMARK_GROUP(14)
BENCHMARK_GROUP(15)
BENCHMARK_GROUP(16)

#undef BENCHMARK_GROUP

BENCHMARK_DRAW_LINE();

//  to_ascii<10>

static void fill_zero_10(size_t iters, size_t index) {
  to_ascii_go<10>(iters, index, [](auto out, auto size, auto v) {
    fill_zero(out, size, v);
  });
}

static void libc_snprintf_10(size_t iters, size_t index) {
  to_ascii_go<10>(iters, index, [](auto out, auto size, auto v) {
    std::snprintf(out, size, "%" PRIu64, v);
  });
}

static void std_to_string_10(size_t iters, size_t index) {
  to_ascii_go<10>(iters, index, [](auto out, auto size, auto v) {
    auto result = std::to_string(v);
    std::memcpy(out, &result[0], size);
  });
}

static void std_to_chars__10(size_t iters, size_t index) {
  to_ascii_go<10>(iters, index, [](auto out, auto size, auto v) {
    call_to_chars(out, out + size, v, 10);
  });
}

static void fmt_format_to_10(size_t iters, size_t index) {
  to_ascii_go<10>(iters, index, [](auto out, auto size, auto v) {
    (void)size;
    fmt::format_to(out, FMT_COMPILE("{}"), v);
  });
}

static void to_ascii_basic_10(size_t iters, size_t index) {
  to_ascii_go<10>(iters, index, [](auto out, auto size, auto v) {
    folly::detail::to_ascii_with_basic<10, abc>(out, size, v);
  });
}

static void to_ascii_array_10(size_t iters, size_t index) {
  to_ascii_go<10>(iters, index, [](auto out, auto size, auto v) {
    folly::detail::to_ascii_with_array<10, abc>(out, size, v);
  });
}

static void to_ascii_table_10(size_t iters, size_t index) {
  to_ascii_go<10>(iters, index, [](auto out, auto size, auto v) {
    folly::detail::to_ascii_with_table<10, abc>(out, size, v);
  });
}

#define BENCHMARK_GROUP(index)              \
  BENCHMARK_DRAW_LINE();                    \
  BENCHMARK_PARAM(fill_zero_10, index)      \
  BENCHMARK_PARAM(libc_snprintf_10, index)  \
  BENCHMARK_PARAM(std_to_string_10, index)  \
  BENCHMARK_PARAM(std_to_chars__10, index)  \
  BENCHMARK_PARAM(fmt_format_to_10, index)  \
  BENCHMARK_PARAM(to_ascii_basic_10, index) \
  BENCHMARK_PARAM(to_ascii_array_10, index) \
  BENCHMARK_PARAM(to_ascii_table_10, index)

BENCHMARK_GROUP(1)
BENCHMARK_GROUP(2)
BENCHMARK_GROUP(3)
BENCHMARK_GROUP(4)
BENCHMARK_GROUP(5)
BENCHMARK_GROUP(6)
BENCHMARK_GROUP(7)
BENCHMARK_GROUP(8)
BENCHMARK_GROUP(9)
BENCHMARK_GROUP(10)
BENCHMARK_GROUP(11)
BENCHMARK_GROUP(12)
BENCHMARK_GROUP(13)
BENCHMARK_GROUP(14)
BENCHMARK_GROUP(15)
BENCHMARK_GROUP(16)
BENCHMARK_GROUP(17)
BENCHMARK_GROUP(18)
BENCHMARK_GROUP(19)
BENCHMARK_GROUP(20)

#undef BENCHMARK_GROUP

BENCHMARK_DRAW_LINE();

//  to_ascii<16>

static void fill_zero_16(size_t iters, size_t index) {
  to_ascii_go<16>(iters, index, [](auto out, auto size, auto v) {
    fill_zero(out, size, v);
  });
}

static void libc_snprintf_16(size_t iters, size_t index) {
  to_ascii_go<16>(iters, index, [](auto out, auto size, auto v) {
    std::snprintf(out, size, "%" PRIu64 "x", v);
  });
}

static void std_to_chars__16(size_t iters, size_t index) {
  to_ascii_go<10>(iters, index, [](auto out, auto size, auto v) {
    call_to_chars(out, out + size, v, 16);
  });
}

static void fmt_format_to_16(size_t iters, size_t index) {
  to_ascii_go<16>(iters, index, [](auto out, auto size, auto v) {
    (void)size;
    fmt::format_to(out, FMT_COMPILE("{:x}"), v);
  });
}

static void to_ascii_basic_16(size_t iters, size_t index) {
  to_ascii_go<16>(iters, index, [](auto out, auto size, auto v) {
    folly::detail::to_ascii_with_basic<16, abc>(out, size, v);
  });
}

static void to_ascii_array_16(size_t iters, size_t index) {
  to_ascii_go<16>(iters, index, [](auto out, auto size, auto v) {
    folly::detail::to_ascii_with_array<16, abc>(out, size, v);
  });
}

static void to_ascii_table_16(size_t iters, size_t index) {
  to_ascii_go<16>(iters, index, [](auto out, auto size, auto v) {
    folly::detail::to_ascii_with_table<16, abc>(out, size, v);
  });
}

#define BENCHMARK_GROUP(index)              \
  BENCHMARK_DRAW_LINE();                    \
  BENCHMARK_PARAM(fill_zero_16, index)      \
  BENCHMARK_PARAM(libc_snprintf_16, index)  \
  BENCHMARK_PARAM(std_to_chars__16, index)  \
  BENCHMARK_PARAM(fmt_format_to_16, index)  \
  BENCHMARK_PARAM(to_ascii_basic_16, index) \
  BENCHMARK_PARAM(to_ascii_array_16, index) \
  BENCHMARK_PARAM(to_ascii_table_16, index)

BENCHMARK_GROUP(1)
BENCHMARK_GROUP(2)
BENCHMARK_GROUP(3)
BENCHMARK_GROUP(4)
BENCHMARK_GROUP(5)
BENCHMARK_GROUP(6)
BENCHMARK_GROUP(7)
BENCHMARK_GROUP(8)
BENCHMARK_GROUP(9)
BENCHMARK_GROUP(10)
BENCHMARK_GROUP(11)
BENCHMARK_GROUP(12)
BENCHMARK_GROUP(13)
BENCHMARK_GROUP(14)
BENCHMARK_GROUP(15)
BENCHMARK_GROUP(16)

#undef BENCHMARK_GROUP

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
