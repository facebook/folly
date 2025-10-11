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

#include <folly/FollyMemchr.h>

#include <cstdint>
#include <cstdlib>
#include <deque>
#include <string>
#include <fmt/core.h>
#include <folly/Benchmark.h>
#include <folly/lang/Bits.h>
#include <folly/Preprocessor.h>
#include <folly/portability/GFlags.h>

DEFINE_uint32(min_size, 1, "Minimum size to benchmark");
DEFINE_uint32(max_size, 32768, "Maximum size to benchmark");
DEFINE_bool(linear, false, "Test all sizes [min_size, max_size]");
DEFINE_uint32(step, 1, "Test sizes step");
DEFINE_uint32(page_offset, 0, "Buffer offset from page aligned size");

static uint8_t* temp_buf;

template <void* memchr_impl(const void*, int, size_t)>
static void bmMemchr(const void* buf, size_t length, size_t iters) {
  for (size_t i = 0; i < iters; ++i) {
    memchr_impl(buf, 0xFF, length);
  }
}

template <void* memchr_impl(const void*, int, size_t)>
static void addMemchrBenchmark(const std::string& name) {
  static std::deque<std::string> names; // lifetime management

  auto addBech = [&](size_t size) {
    names.emplace_back(fmt::format("{}: size={}", name, size));
    folly::addBenchmark(__FILE__, names.back().c_str(), [=](unsigned iters) {
      bmMemchr<memchr_impl>(temp_buf + FLAGS_page_offset, size, iters);
      return iters;
    });
  };

  if (FLAGS_linear) {
    for (size_t size = FLAGS_min_size; size <= FLAGS_max_size;
         size += FLAGS_step) {
      addBech(size);
    }
  } else {
    for (size_t size = folly::prevPowTwo(FLAGS_min_size); size <= folly::prevPowTwo(FLAGS_max_size);
         size <<= 1) {
      addBech(size);
    }
  }

  /* Draw line. */
  folly::addBenchmark(__FILE__, "-", []() { return 0; });
}

// Only to fool compiler optimizer not to optimize out unused results, could 
// have used pragmas for that
static uint64_t result_offset = 0;
static FOLLY_NOINLINE void* std_memchr(const void *s, int c, size_t l) {
  folly::compiler_must_not_predict(result_offset);
  result_offset =  (uint64_t)std::memchr((void *)s, c, l)-(uint64_t)s;
  return (void*)(s + result_offset);
}

static FOLLY_NOINLINE void* folly_memchr(const void *s, int c, size_t l) {
  folly::compiler_must_not_predict(result_offset);
  result_offset =  (uint64_t)folly::memchr_long((void *)s, c, l)-(uint64_t)s;
  return (void*)(s + result_offset);
}

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  assert(FLAGS_min_size <= FLAGS_max_size);
  assert(FLAGS_page_offset < 4096);
  assert(FLAGS_step > 0);

  size_t totalBufSize = (FLAGS_max_size + FLAGS_page_offset + 4095) & ~4095;
  temp_buf = (uint8_t*)aligned_alloc(4096, totalBufSize);
  // Make sure all pages are allocated
  for (size_t i = 0; i < totalBufSize; i++) {
    temp_buf[i] = 0;
  }
  temp_buf[totalBufSize-1] = 0xFF;

#define BENCHMARK_MEMCHR(MEMCHR) \
  addMemchrBenchmark<MEMCHR>(FOLLY_PP_STRINGIZE(MEMCHR));

  BENCHMARK_MEMCHR(std_memchr);
  BENCHMARK_MEMCHR(folly_memchr);

  folly::runBenchmarks();

  free(temp_buf);

  return 0;
}
