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
#include <folly/lang/Align.h>
#include <folly/portability/Unistd.h>

#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__x86_64__)
#include <x86intrin.h>
#endif

static auto page_size = sysconf(_SC_PAGE_SIZE);
static auto cache_line_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);

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
        page_size *
        std::ceil(static_cast<double>(max + 2 * align) / page_size));
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

static inline void* ptr_bytewise_arith(void* p, ptrdiff_t diff) noexcept {
  auto addr = reinterpret_cast<uint8_t*>(p);
  auto new_addr = addr + diff;
  return reinterpret_cast<void*>(new_addr);
}

void scalar_memops(
    uint32_t iters,
    uint32_t alignment,
    bool aligned,
    bool read_only,
    bool stlf) { // Store-To-Load Forwarding
  std::vector<uintptr_t> raw_buffer;
  volatile uintptr_t* ptr = nullptr;
  volatile uint16_t* write_ptr = nullptr;
  uintptr_t value;

  BENCHMARK_SUSPEND {
    raw_buffer.resize(page_size * 3);
    ptr = static_cast<volatile uintptr_t*>(
        folly::align_ceil(raw_buffer.data(), page_size));

    ptr = static_cast<volatile uintptr_t*>(
        ptr_bytewise_arith(const_cast<uintptr_t*>(ptr), alignment));

    if (!aligned) {
      ptr = reinterpret_cast<uintptr_t*>(
          ptr_bytewise_arith(const_cast<uintptr_t*>(ptr), -1));
    }

    *ptr = reinterpret_cast<uintptr_t>(ptr);
  }

  // Use pointer-chasing to make sure each mem_op depends on the
  // previou mem_op.
  if (read_only) {
    FOLLY_PRAGMA_UNROLL_N(16)
    for (unsigned int i = 0; i < iters; i++) {
      value = *ptr;
      // NOLINTNEXTLINE(performance-no-int-to-ptr)
      ptr = reinterpret_cast<uintptr_t*>(value);
    }
  } else {
    if (stlf) {
      FOLLY_PRAGMA_UNROLL_N(16)
      for (unsigned int i = 0; i < iters; i++) {
        value = *ptr;
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        ptr = reinterpret_cast<uintptr_t*>(value);
        *ptr = value;
      }
    } else {
      // STLF is usually not enabled if the write is less than half of
      // memory size than the read.
      static_assert(sizeof(uint16_t) < sizeof(uintptr_t) / 2);
      FOLLY_PRAGMA_UNROLL_N(16)
      for (unsigned int i = 0; i < iters; i++) {
        value = *ptr;
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        ptr = reinterpret_cast<uintptr_t*>(value);
        write_ptr = reinterpret_cast<volatile uint16_t*>(ptr);
        *write_ptr = static_cast<uint16_t>(value);
      }
    }
  }
}

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    scalar_memops, aligned_reads, page_size, true, true, false)
BENCHMARK_NAMED_PARAM(
    scalar_memops, cache_unaligned_reads, cache_line_size, false, true, false)
BENCHMARK_NAMED_PARAM(
    scalar_memops, page_unaligned_reads, page_size, false, true, false)
BENCHMARK_NAMED_PARAM(
    scalar_memops, aligned_reads_writes, page_size, true, false, false)
BENCHMARK_NAMED_PARAM(
    scalar_memops,
    cache_unaligned_reads_writes,
    cache_line_size,
    false,
    false,
    false)
BENCHMARK_NAMED_PARAM(
    scalar_memops, page_unaligned_reads_writes, page_size, false, false, false)
BENCHMARK_NAMED_PARAM(
    scalar_memops, aligned_reads_writes_stlf, page_size, true, false, true)
BENCHMARK_NAMED_PARAM(
    scalar_memops,
    cache_unaligned_reads_writes_stlf,
    cache_line_size,
    false,
    false,
    true)
BENCHMARK_NAMED_PARAM(
    scalar_memops,
    page_unaligned_reads_writes_stlf,
    page_size,
    false,
    false,
    true)

#if defined(__aarch64__)

void neon_memops(
    uint32_t iters,
    size_t alignment,
    bool aligned,
    bool read_only,
    bool stlf) { // Store-To-Load Forwarding
  std::vector<uint64x2_t> raw_buffer;
  uint64_t* ptr = nullptr;
  uint32_t* write_ptr = nullptr;
  uint64x2_t value;

  BENCHMARK_SUSPEND {
    raw_buffer.resize(page_size * 3);
    ptr = reinterpret_cast<uint64_t*>(
        folly::align_ceil(raw_buffer.data(), page_size));

    ptr = static_cast<uint64_t*>(ptr_bytewise_arith(ptr, alignment));

    if (!aligned) {
      ptr = reinterpret_cast<uint64_t*>(ptr_bytewise_arith(ptr, -1));
    }
    value = vcombine_u64(
        vcreate_u64(reinterpret_cast<uint64_t>(ptr)),
        vcreate_u64(reinterpret_cast<uint64_t>(ptr)));
    vst1q_u64(ptr, value);
  }

  // Use pointer-chasing to make sure each read depends on the
  // previou read/write.
  if (read_only) {
    FOLLY_PRAGMA_UNROLL_N(16)
    for (unsigned int i = 0; i < iters; i++) {
      folly::doNotOptimizeAway(value = vld1q_u64(ptr));
      ptr = reinterpret_cast<uint64_t*>(vgetq_lane_u64(value, 0));
    }
  } else {
    if (stlf) {
      FOLLY_PRAGMA_UNROLL_N(16)
      for (unsigned int i = 0; i < iters; i++) {
        folly::doNotOptimizeAway(value = vld1q_u64(ptr));
        ptr = reinterpret_cast<uint64_t*>(vgetq_lane_u64(value, 0));
        vst1q_u64(ptr, value);
      }
    } else {
      // STLF is usually not enabled if the write is less than half of
      // memory size than the read.
      FOLLY_PRAGMA_UNROLL_N(16)
      for (unsigned int i = 0; i < iters; i++) {
        folly::doNotOptimizeAway(value = vld1q_u64(ptr));
        ptr = reinterpret_cast<uint64_t*>(vgetq_lane_u64(value, 0));
        write_ptr = reinterpret_cast<uint32_t*>(ptr);
        vst1q_lane_u32(write_ptr, value, 0);
      }
    }
  }
}

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(neon_memops, aligned_reads, page_size, true, true, false)
BENCHMARK_NAMED_PARAM(
    neon_memops, cache_unaligned_reads, cache_line_size, false, true, false)
BENCHMARK_NAMED_PARAM(
    neon_memops, page_unaligned_reads, page_size, false, true, false)
BENCHMARK_NAMED_PARAM(
    neon_memops, aligned_reads_writes, page_size, true, false, false)
BENCHMARK_NAMED_PARAM(
    neon_memops,
    cache_unaligned_reads_writes,
    cache_line_size,
    false,
    false,
    false)
BENCHMARK_NAMED_PARAM(
    neon_memops, page_unaligned_reads_writes, page_size, false, false, false)

BENCHMARK_NAMED_PARAM(
    neon_memops, aligned_reads_writes_stlf, page_size, true, false, true)
BENCHMARK_NAMED_PARAM(
    neon_memops,
    cache_unaligned_reads_writes_stlf,
    cache_line_size,
    false,
    false,
    true)
BENCHMARK_NAMED_PARAM(
    neon_memops,
    page_unaligned_reads_writes_stlf,
    page_size,
    false,
    false,
    true)

#elif defined(__x86_64__) && defined(__AVX2__)

static inline uint64_t get_low64(__m256i v) {
  __m128i low128 = _mm256_castsi256_si128(v);
  return _mm_cvtsi128_si64(low128); // extracts the lowest 64 bits
}

void avx2_memops(
    uint32_t iters,
    uint32_t alignment,
    bool aligned,
    bool read_only,
    bool stlf) { // Store-To-Load Forwarding
  std::vector<__m256i> raw_buffer;
  __m256i* ptr = nullptr;
  __m128i* write_ptr = nullptr;
  __m256i value;

  BENCHMARK_SUSPEND {
    raw_buffer.resize(page_size * 3);
    ptr =
        static_cast<__m256i*>(folly::align_ceil(raw_buffer.data(), page_size));

    ptr = static_cast<__m256i*>(ptr_bytewise_arith(ptr, alignment));

    if (!aligned) {
      ptr = reinterpret_cast<__m256i*>(
          ptr_bytewise_arith(ptr, -(sizeof(uint64_t) + 1)));
    }

    static_assert(sizeof(uintptr_t) * 4 == sizeof(__m256i));
    value = _mm256_set_epi64x(
        reinterpret_cast<uintptr_t>(ptr),
        reinterpret_cast<uintptr_t>(ptr),
        reinterpret_cast<uintptr_t>(ptr),
        reinterpret_cast<uintptr_t>(ptr));

    _mm256_storeu_si256(ptr, value);
  }

  // Use pointer-chasing to make sure each mem_op depends on the
  // previou one.
  if (read_only) {
    FOLLY_PRAGMA_UNROLL_N(16)
    for (unsigned int i = 0; i < iters; i++) {
      folly::doNotOptimizeAway(value = _mm256_loadu_si256(ptr));
      // NOLINTNEXTLINE(performance-no-int-to-ptr)
      ptr = reinterpret_cast<__m256i*>(get_low64(value));
    }
  } else {
    if (stlf) {
      FOLLY_PRAGMA_UNROLL_N(16)
      for (unsigned int i = 0; i < iters; i++) {
        folly::doNotOptimizeAway(value = _mm256_loadu_si256(ptr));
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        ptr = reinterpret_cast<__m256i*>(get_low64(value));
        _mm256_storeu_si256(ptr, value);
      }
    } else {
      FOLLY_PRAGMA_UNROLL_N(16)
      for (unsigned int i = 0; i < iters; i++) {
        folly::doNotOptimizeAway(value = _mm256_loadu_si256(ptr));
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        write_ptr = reinterpret_cast<__m128i*>(get_low64(value));
        write_ptr = reinterpret_cast<__m128i*>(
            ptr_bytewise_arith(write_ptr, sizeof(uint64_t)));
        _mm_storeu_si128(write_ptr, _mm256_castsi256_si128(value));
      }
    }
  }
}

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(avx2_memops, aligned_reads, page_size, true, true, false)
BENCHMARK_NAMED_PARAM(
    avx2_memops, cache_unaligned_reads, cache_line_size, false, true, false)
BENCHMARK_NAMED_PARAM(
    avx2_memops, page_unaligned_reads, page_size, false, true, false)

BENCHMARK_NAMED_PARAM(
    avx2_memops, aligned_reads_writes, page_size, true, false, false)
BENCHMARK_NAMED_PARAM(
    avx2_memops,
    cache_unaligned_reads_writes,
    cache_line_size,
    false,
    false,
    false)
BENCHMARK_NAMED_PARAM(
    avx2_memops, page_unaligned_reads_writes, page_size, false, false, false)

BENCHMARK_NAMED_PARAM(
    avx2_memops, aligned_reads_writes_stlf, page_size, true, false, true)
BENCHMARK_NAMED_PARAM(
    avx2_memops,
    cache_unaligned_reads_writes_stlf,
    cache_line_size,
    false,
    false,
    true)
BENCHMARK_NAMED_PARAM(
    avx2_memops,
    page_unaligned_reads_writes_stlf,
    page_size,
    false,
    false,
    true)

#endif

// Benchmark results (Arm Neoverse V2, Linux Arm64, 3.4GHz)
// Buck build mode: @mode/opt
// ============================================================================
// fbcode/folly/test/MemcpyBenchmark.cpp     relative  time/iter   iters/s
// ============================================================================
// bench(0_to_7_HOT_glibc)                                     2.21ns   452.91M
// bench(0_to_7_HOT_folly)                         101.21%     2.18ns   458.38M
// bench(0_to_16_HOT_glibc)                                    2.26ns   442.25M
// bench(0_to_16_HOT_folly)                        99.207%     2.28ns   438.74M
// bench(0_to_32_HOT_glibc)                                    2.42ns   413.08M
// bench(0_to_32_HOT_folly)                        99.140%     2.44ns   409.53M
// bench(0_to_64_HOT_glibc)                                    2.96ns   337.84M
// bench(0_to_64_HOT_folly)                        98.762%     3.00ns   333.66M
// bench(0_to_128_HOT_glibc)                                   3.67ns   272.49M
// bench(0_to_128_HOT_folly)                       98.843%     3.71ns   269.33M
// bench(0_to_256_HOT_glibc)                                   5.77ns   173.26M
// bench(0_to_256_HOT_folly)                       99.561%     5.80ns   172.50M
// bench(0_to_512_HOT_glibc)                                   8.13ns   123.06M
// bench(0_to_512_HOT_folly)                       99.095%     8.20ns   121.95M
// bench(0_to_1024_HOT_glibc)                                 11.34ns    88.19M
// bench(0_to_1024_HOT_folly)                      99.640%    11.38ns    87.87M
// bench(0_to_32768_HOT_glibc)                               173.08ns     5.78M
// bench(0_to_32768_HOT_folly)                     99.928%   173.20ns     5.77M
// bench(8_to_16_HOT_glibc)                                    2.32ns   430.99M
// bench(8_to_16_HOT_folly)                        98.794%     2.35ns   425.79M
// bench(16_to_32_HOT_glibc)                                   2.55ns   392.04M
// bench(16_to_32_HOT_folly)                       99.618%     2.56ns   390.55M
// bench(32_to_256_HOT_glibc)                                  5.80ns   172.42M
// bench(32_to_256_HOT_folly)                      100.00%     5.80ns   172.42M
// bench(256_to_1024_HOT_glibc)                               12.82ns    77.99M
// bench(256_to_1024_HOT_folly)                    99.665%    12.86ns    77.73M
// bench(1024_to_8192_HOT_glibc)                              53.06ns    18.85M
// bench(1024_to_8192_HOT_folly)                   99.941%    53.09ns    18.84M
// ----------------------------------------------------------------------------
// bench(0_to_7_COLD_glibc)                                    3.74ns   267.71M
// bench(0_to_7_COLD_folly)                        104.42%     3.58ns   279.55M
// bench(0_to_16_COLD_glibc)                                   3.97ns   251.91M
// bench(0_to_16_COLD_folly)                       94.755%     4.19ns   238.69M
// bench(0_to_32_COLD_glibc)                                   4.41ns   226.95M
// bench(0_to_32_COLD_folly)                       119.84%     3.68ns   271.98M
// bench(0_to_64_COLD_glibc)                                   6.88ns   145.39M
// bench(0_to_64_COLD_folly)                       115.40%     5.96ns   167.78M
// bench(0_to_128_COLD_glibc)                                 13.51ns    74.04M
// bench(0_to_128_COLD_folly)                      93.560%    14.44ns    69.28M
// bench(0_to_256_COLD_glibc)                                 31.71ns    31.54M
// bench(0_to_256_COLD_folly)                      92.840%    34.15ns    29.28M
// bench(0_to_512_COLD_glibc)                                 54.56ns    18.33M
// bench(0_to_512_COLD_folly)                      102.43%    53.26ns    18.78M
// bench(0_to_1024_COLD_glibc)                                94.15ns    10.62M
// bench(0_to_1024_COLD_folly)                     105.42%    89.31ns    11.20M
// bench(0_to_32768_COLD_glibc)                              401.09ns     2.49M
// bench(0_to_32768_COLD_folly)                    99.907%   401.47ns     2.49M
// bench(8_to_16_COLD_glibc)                                   4.04ns   247.70M
// bench(8_to_16_COLD_folly)                       103.82%     3.89ns   257.16M
// bench(16_to_32_COLD_glibc)                                  4.43ns   225.55M
// bench(16_to_32_COLD_folly)                      69.386%     6.39ns   156.50M
// bench(32_to_256_COLD_glibc)                                36.54ns    27.37M
// bench(32_to_256_COLD_folly)                     104.05%    35.12ns    28.47M
// bench(256_to_1024_COLD_glibc)                              98.92ns    10.11M
// bench(256_to_1024_COLD_folly)                   98.507%   100.42ns     9.96M
// bench(1024_to_8192_COLD_glibc)                            219.71ns     4.55M
// bench(1024_to_8192_COLD_folly)                  100.31%   219.02ns     4.57M
// ----------------------------------------------------------------------------
// scalar_memops(aligned_reads)                              904.58ps     1.11G
// scalar_memops(cache_unaligned_reads)                        1.21ns   828.79M
// scalar_memops(page_unaligned_reads)                         1.21ns   828.78M
// scalar_memops(aligned_reads_writes)                         3.53ns   283.00M
// scalar_memops(cache_unaligned_reads_writes)                 4.85ns   206.06M
// scalar_memops(page_unaligned_reads_writes)                  6.45ns   155.12M
// scalar_memops(aligned_reads_writes_stlf)                    2.01ns   496.65M
// scalar_memops(cache_unaligned_reads_writes_stlf             2.52ns   397.39M
// scalar_memops(page_unaligned_reads_writes_stlf)             6.45ns   155.12M
// ----------------------------------------------------------------------------
// neon_memops(aligned_reads)                                  2.72ns   368.17M
// neon_memops(cache_unaligned_reads)                          3.02ns   331.24M
// neon_memops(page_unaligned_reads)                           3.02ns   331.14M
// neon_memops(aligned_reads_writes)                           7.12ns   140.40M
// neon_memops(cache_unaligned_reads_writes)                   8.31ns   120.36M
// neon_memops(page_unaligned_reads_writes)                    8.48ns   117.90M
// neon_memops(aligned_reads_writes_stlf)                      1.76ns   567.21M
// neon_memops(cache_unaligned_reads_writes_stlf)              1.76ns   568.63M
// neon_memops(page_unaligned_reads_writes_stlf)               3.21ns   311.11M
// ============================================================================

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
