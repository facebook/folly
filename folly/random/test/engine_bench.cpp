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

#include <folly/random/xoshiro256pp.h>

#include <cstdint>
#include <random>

#include <folly/Benchmark.h>
#include <folly/lang/Keep.h>
#include <folly/portability/Config.h>

#if defined(FOLLY_HAVE_EXTRANDOM_SFMT19937) && FOLLY_HAVE_EXTRANDOM_SFMT19937
#include <ext/random>
#endif

extern "C" FOLLY_KEEP void check_folly_xoshiro256pp_64_seed_value(
    folly::xoshiro256pp_64& rng, folly::xoshiro256pp_64::result_type val) {
  rng.seed(val);
}

extern "C" FOLLY_KEEP void check_folly_xoshiro256pp_64_seed_std_seed_seq(
    folly::xoshiro256pp_64& rng, std::seed_seq& seq) {
  rng.seed(seq);
}

extern "C" FOLLY_KEEP uint64_t
check_folly_xoshiro256pp_64_next(folly::xoshiro256pp_64& rng) {
  return rng();
}

extern "C" FOLLY_KEEP void check_folly_xoshiro256pp_64_fill(
    folly::xoshiro256pp_64& rng, std::uint64_t* out, size_t len) {
  while (len--) {
    *out++ = rng();
  }
}

template <typename RNG>
void bm_seed_value(size_t iters) {
  folly::BenchmarkSuspender braces;
  RNG rng;
  typename RNG::result_type seed_val = 0;
  braces.dismissing([&] {
    while (iters--) {
      rng.seed(seed_val++);
      folly::compiler_must_not_elide(rng);
    }
  });
}

template <typename RNG>
void bm_seed_std_seed_seq(size_t iters) {
  folly::BenchmarkSuspender braces;
  RNG rng;
  std::seed_seq seq({0xdeadbeef});
  braces.dismissing([&] {
    while (iters--) {
      rng.seed(seq);
      folly::compiler_must_not_elide(rng);
    }
  });
}

template <typename RNG>
void bm_next(size_t iters) {
  folly::BenchmarkSuspender braces;
  RNG rng;
  typename RNG::result_type res = 0;

  braces.dismissing([&] {
    while (iters--) {
      res ^= rng();
    }
  });
  folly::compiler_must_not_elide(res);
}

BENCHMARK(seed_value_std_minstd_rand, iters) {
  using rng_t = std::minstd_rand;
  bm_seed_value<rng_t>(iters);
}

BENCHMARK(seed_value_std_mt19937, iters) {
  using rng_t = std::mt19937;
  bm_seed_value<rng_t>(iters);
}

BENCHMARK(seed_value_std_mt19937_64, iters) {
  using rng_t = std::mt19937_64;
  bm_seed_value<rng_t>(iters);
}

#if defined(FOLLY_HAVE_EXTRANDOM_SFMT19937) && FOLLY_HAVE_EXTRANDOM_SFMT19937

BENCHMARK(seed_value_gnu_cxx_sfmt19937, iters) {
  using rng_t = __gnu_cxx::sfmt19937;
  bm_seed_value<rng_t>(iters);
}

BENCHMARK(seed_value_gnu_cxx_sfmt19937_64, iters) {
  using rng_t = __gnu_cxx::sfmt19937_64;
  bm_seed_value<rng_t>(iters);
}

#endif

BENCHMARK(seed_value_folly_xoshiro256pp_64, iters) {
  using rng_t = folly::xoshiro256pp_64;
  bm_seed_value<rng_t>(iters);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(seed_std_seed_seq_std_minstd_rand, iters) {
  using rng_t = std::minstd_rand;
  bm_seed_std_seed_seq<rng_t>(iters);
}

BENCHMARK(seed_std_seed_seq_std_mt19937, iters) {
  using rng_t = std::mt19937;
  bm_seed_std_seed_seq<rng_t>(iters);
}

BENCHMARK(seed_std_seed_seq_std_mt19937_64, iters) {
  using rng_t = std::mt19937_64;
  bm_seed_std_seed_seq<rng_t>(iters);
}

#if defined(FOLLY_HAVE_EXTRANDOM_SFMT19937) && FOLLY_HAVE_EXTRANDOM_SFMT19937

BENCHMARK(seed_std_seed_seq_gnu_cxx_sfmt19937, iters) {
  using rng_t = __gnu_cxx::sfmt19937;
  bm_seed_std_seed_seq<rng_t>(iters);
}

BENCHMARK(seed_std_seed_seq_gnu_cxx_sfmt19937_64, iters) {
  using rng_t = __gnu_cxx::sfmt19937_64;
  bm_seed_std_seed_seq<rng_t>(iters);
}

#endif

BENCHMARK(seed_std_seed_seq_folly_xoshiro256pp_64, iters) {
  using rng_t = folly::xoshiro256pp_64;
  bm_seed_std_seed_seq<rng_t>(iters);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(next_std_minstd_rand, iters) {
  using rng_t = std::minstd_rand;
  bm_next<rng_t>(iters);
}

BENCHMARK(next_std_mt19937, iters) {
  using rng_t = std::mt19937;
  bm_next<rng_t>(iters);
}

BENCHMARK(next_std_mt19937_64, iters) {
  using rng_t = std::mt19937_64;
  bm_next<rng_t>(iters);
}

#if defined(FOLLY_HAVE_EXTRANDOM_SFMT19937) && FOLLY_HAVE_EXTRANDOM_SFMT19937

BENCHMARK(next_gnu_cxx_sfmt19937, iters) {
  using rng_t = __gnu_cxx::sfmt19937;
  bm_next<rng_t>(iters);
}

BENCHMARK(next_gnu_cxx_sfmt19937_64, iters) {
  using rng_t = __gnu_cxx::sfmt19937_64;
  bm_next<rng_t>(iters);
}

#endif

BENCHMARK(next_folly_xoshiro256pp_64, iters) {
  using rng_t = folly::xoshiro256pp_64;
  bm_next<rng_t>(iters);
}

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
