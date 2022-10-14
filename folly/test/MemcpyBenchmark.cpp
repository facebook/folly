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

#include <string.h>

#include <chrono>
#include <random>

#include <folly/Benchmark.h>
#include <folly/FollyMemcpy.h>
#include <folly/portability/Unistd.h>

void bench(
    uint32_t iters,
    void*(memcpy_func)(void*, const void*, size_t),
    size_t min,
    size_t max,
    size_t align,
    bool hot) {
  static std::string dst_buffer;
  static std::string src_buffer;
  static std::vector<size_t> sizes;
  static std::vector<size_t> dst_offsets;
  static std::vector<size_t> src_offsets;

  BENCHMARK_SUSPEND {
    size_t src_buffer_size = folly::to_integral(
        sysconf(_SC_PAGE_SIZE) *
        std::ceil(
            static_cast<double>(max + 2 * align) / sysconf(_SC_PAGE_SIZE)));
    size_t dst_buffer_size;
    if (hot) {
      dst_buffer_size = src_buffer_size;
    } else {
      dst_buffer_size = 1024 * 1024 * 1024; // 1 GiB
    }
    dst_buffer.resize(dst_buffer_size);
    memset(dst_buffer.data(), 'd', dst_buffer.size());
    src_buffer.resize(src_buffer_size);
    memset(src_buffer.data(), 's', src_buffer.size());

    std::default_random_engine gen;
    sizes.resize(4095);
    std::uniform_int_distribution<size_t> size_dist(min, max);
    for (size_t i = 0; i < sizes.size(); i++) {
      sizes[i] = size_dist(gen);
    }

    src_offsets.resize(4096);
    dst_offsets.resize(4096);
    std::uniform_int_distribution<size_t> src_offset_dist(
        0, (src_buffer_size - max) / align);
    std::uniform_int_distribution<size_t> dst_offset_dist(
        0, (dst_buffer_size - max) / align);
    for (size_t i = 0; i < src_offsets.size(); i++) {
      src_offsets[i] = align * src_offset_dist(gen);
      dst_offsets[i] = align * dst_offset_dist(gen);
    }
  }

  size_t size_idx = 0;
  size_t offset_idx = 0;
  for (unsigned int i = 0; i < iters; i++) {
    if (size_idx + 1 == sizes.size()) {
      size_idx = 0;
    }
    if (offset_idx >= src_offsets.size()) {
      offset_idx = 0;
    }
    void* dst = &dst_buffer[dst_offsets[offset_idx]];
    const void* src = &src_buffer[src_offsets[offset_idx]];
    size_t size = sizes[size_idx];
    memcpy_func(dst, src, size);
    size_idx++;
    offset_idx++;
  }
}

#define BENCH_BOTH(MIN, MAX, HOT, HOT_STR)   \
  BENCHMARK_NAMED_PARAM(                     \
      bench,                                 \
      MIN##_to_##MAX##_##HOT_STR##_glibc,    \
      /*memcpy_func=*/memcpy,                \
      /*min=*/MIN,                           \
      /*max=*/MAX,                           \
      /*align=*/1,                           \
      /*hot=*/HOT)                           \
  BENCHMARK_RELATIVE_NAMED_PARAM(            \
      bench,                                 \
      MIN##_to_##MAX##_##HOT_STR##_folly,    \
      /*memcpy_func=*/folly::__folly_memcpy, \
      /*min=*/MIN,                           \
      /*max=*/MAX,                           \
      /*align=*/1,                           \
      /*hot=*/HOT)

BENCH_BOTH(0, 7, true, HOT)
BENCH_BOTH(0, 16, true, HOT)
BENCH_BOTH(0, 32, true, HOT)
BENCH_BOTH(0, 64, true, HOT)
BENCH_BOTH(0, 128, true, HOT)
BENCH_BOTH(0, 256, true, HOT)
BENCH_BOTH(0, 512, true, HOT)
BENCH_BOTH(0, 1024, true, HOT)
BENCH_BOTH(0, 32768, true, HOT)
BENCH_BOTH(8, 16, true, HOT)
BENCH_BOTH(16, 32, true, HOT)
BENCH_BOTH(32, 256, true, HOT)
BENCH_BOTH(256, 1024, true, HOT)
BENCH_BOTH(1024, 8192, true, HOT)

BENCHMARK_DRAW_LINE();
BENCH_BOTH(0, 7, false, COLD)
BENCH_BOTH(0, 16, false, COLD)
BENCH_BOTH(0, 32, false, COLD)
BENCH_BOTH(0, 64, false, COLD)
BENCH_BOTH(0, 128, false, COLD)
BENCH_BOTH(0, 256, false, COLD)
BENCH_BOTH(0, 512, false, COLD)
BENCH_BOTH(0, 1024, false, COLD)
BENCH_BOTH(0, 32768, false, COLD)
BENCH_BOTH(8, 16, false, COLD)
BENCH_BOTH(16, 32, false, COLD)
BENCH_BOTH(32, 256, false, COLD)
BENCH_BOTH(256, 1024, false, COLD)
BENCH_BOTH(1024, 8192, false, COLD)

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    bench,
    64k_to_1024k_unaligned_cold_glibc,
    /*memcpy_func=*/memcpy,
    /*min=*/65536,
    /*max=*/1048576,
    /*align=*/1,
    /*hot=*/false)
BENCHMARK_RELATIVE_NAMED_PARAM(
    bench,
    64k_to_1024k_unaligned_cold_folly,
    /*memcpy_func=*/folly::__folly_memcpy,
    /*min=*/65536,
    /*max=*/1048576,
    /*align=*/1,
    /*hot=*/false)

BENCHMARK_NAMED_PARAM(
    bench,
    64k_to_1024k_aligned_cold_glibc,
    /*memcpy_func=*/memcpy,
    /*min=*/65536,
    /*max=*/1048576,
    /*align=*/32,
    /*hot=*/false)
BENCHMARK_RELATIVE_NAMED_PARAM(
    bench,
    64k_to_1024k_aligned_cold_folly,
    /*memcpy_func=*/folly::__folly_memcpy,
    /*min=*/65536,
    /*max=*/1048576,
    /*align=*/32,
    /*hot=*/false)

// Benchmark results (Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz, Linux x86_64)
// Buck build mode: @mode/opt-lto
// ============================================================================
// folly/test/MemcpyBenchmark.cpp                  relative  time/iter  iters/s
// ============================================================================
// bench(0_to_7_HOT_glibc)                                      9.51ns  105.19M
// bench(0_to_7_HOT_folly)                          142.33%     6.68ns  149.72M
// bench(0_to_16_HOT_glibc)                                     8.98ns  111.30M
// bench(0_to_16_HOT_folly)                         153.23%     5.86ns  170.55M
// bench(0_to_32_HOT_glibc)                                     9.08ns  110.08M
// bench(0_to_32_HOT_folly)                         166.79%     5.45ns  183.61M
// bench(0_to_64_HOT_glibc)                                     8.35ns  119.79M
// bench(0_to_64_HOT_folly)                         124.48%     6.71ns  149.11M
// bench(0_to_128_HOT_glibc)                                    8.20ns  122.00M
// bench(0_to_128_HOT_folly)                        121.55%     6.74ns  148.29M
// bench(0_to_256_HOT_glibc)                                    8.64ns  115.68M
// bench(0_to_256_HOT_folly)                         95.85%     9.02ns  110.88M
// bench(0_to_512_HOT_glibc)                                   13.05ns   76.61M
// bench(0_to_512_HOT_folly)                        110.04%    11.86ns   84.31M
// bench(0_to_1024_HOT_glibc)                                  16.00ns   62.50M
// bench(0_to_1024_HOT_folly)                       100.53%    15.91ns   62.83M
// bench(0_to_32768_HOT_glibc)                                658.76ns    1.52M
// bench(0_to_32768_HOT_folly)                      112.30%   586.62ns    1.70M
// bench(8_to_16_HOT_glibc)                                     5.18ns  193.08M
// bench(8_to_16_HOT_folly)                         162.18%     3.19ns  313.13M
// bench(16_to_32_HOT_glibc)                                    4.55ns  219.65M
// bench(16_to_32_HOT_folly)                        117.18%     3.89ns  257.39M
// bench(32_to_256_HOT_glibc)                                   8.70ns  114.98M
// bench(32_to_256_HOT_folly)                        95.64%     9.09ns  109.97M
// bench(256_to_1024_HOT_glibc)                                16.59ns   60.28M
// bench(256_to_1024_HOT_folly)                      96.15%    17.25ns   57.96M
// bench(1024_to_8192_HOT_glibc)                              111.93ns    8.93M
// bench(1024_to_8192_HOT_folly)                    135.92%    82.35ns   12.14M
// ----------------------------------------------------------------------------
// bench(0_to_7_COLD_glibc)                                   101.72ns    9.83M
// bench(0_to_7_COLD_folly)                         242.15%    42.01ns   23.81M
// bench(0_to_16_COLD_glibc)                                  105.14ns    9.51M
// bench(0_to_16_COLD_folly)                        244.61%    42.98ns   23.26M
// bench(0_to_32_COLD_glibc)                                  108.45ns    9.22M
// bench(0_to_32_COLD_folly)                        238.48%    45.48ns   21.99M
// bench(0_to_64_COLD_glibc)                                  102.38ns    9.77M
// bench(0_to_64_COLD_folly)                        192.08%    53.30ns   18.76M
// bench(0_to_128_COLD_glibc)                                 122.86ns    8.14M
// bench(0_to_128_COLD_folly)                       198.17%    62.00ns   16.13M
// bench(0_to_256_COLD_glibc)                                 125.43ns    7.97M
// bench(0_to_256_COLD_folly)                       154.93%    80.96ns   12.35M
// bench(0_to_512_COLD_glibc)                                 161.50ns    6.19M
// bench(0_to_512_COLD_folly)                       149.92%   107.72ns    9.28M
// bench(0_to_1024_COLD_glibc)                                229.68ns    4.35M
// bench(0_to_1024_COLD_folly)                      141.36%   162.48ns    6.15M
// bench(0_to_32768_COLD_glibc)                                 2.91us  343.90K
// bench(0_to_32768_COLD_folly)                     138.83%     2.09us  477.42K
// bench(8_to_16_COLD_glibc)                                  115.47ns    8.66M
// bench(8_to_16_COLD_folly)                        242.11%    47.69ns   20.97M
// bench(16_to_32_COLD_glibc)                                 103.71ns    9.64M
// bench(16_to_32_COLD_folly)                       207.16%    50.06ns   19.98M
// bench(32_to_256_COLD_glibc)                                141.85ns    7.05M
// bench(32_to_256_COLD_folly)                      179.79%    78.90ns   12.67M
// bench(256_to_1024_COLD_glibc)                              236.81ns    4.22M
// bench(256_to_1024_COLD_folly)                    110.72%   213.88ns    4.68M
// bench(1024_to_8192_COLD_glibc)                             911.56ns    1.10M
// bench(1024_to_8192_COLD_folly)                   120.27%   757.90ns    1.32M
// ----------------------------------------------------------------------------
// bench(64k_to_1024k_unaligned_cold_glibc)                    70.17us   14.25K
// bench(64k_to_1024k_unaligned_cold_folly)         129.15%    54.34us   18.40K
// bench(64k_to_1024k_aligned_cold_glibc)                      69.28us   14.43K
// bench(64k_to_1024k_aligned_cold_folly)           246.52%    28.10us   35.58K
// ============================================================================

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
