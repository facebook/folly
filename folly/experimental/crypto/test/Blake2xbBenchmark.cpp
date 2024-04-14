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

#include <folly/experimental/crypto/Blake2xb.h>

#include <vector>

#include <sodium.h>

#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/init/Init.h>
#include <folly/io/IOBuf.h>

#include <glog/logging.h>

using namespace ::folly::crypto;

void benchmarkBlake2b(size_t inputSize, size_t n) {
  std::array<uint8_t, crypto_generichash_blake2b_BYTES_MAX> result;
  std::vector<uint8_t> input;
  BENCHMARK_SUSPEND {
    input.resize(inputSize);
  };
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    int res = crypto_generichash_blake2b(
        result.data(), sizeof(result), input.data(), input.size(), nullptr, 0);
    if (res != 0) {
      throw std::runtime_error("blake2b hash failed");
    }
  }
}

void benchmarkBlake2bMultiple(size_t inputSize, size_t m, size_t n) {
  std::vector<uint8_t> input;
  std::vector<uint8_t> output;
  std::vector<uint8_t> personalization;
  std::array<uint8_t, crypto_generichash_blake2b_BYTES_MAX> h0;
  BENCHMARK_SUSPEND {
    output.resize(crypto_generichash_blake2b_BYTES_MAX * m);
    input.resize(inputSize);
    personalization.resize(crypto_generichash_blake2b_PERSONALBYTES);
  };
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    int res = crypto_generichash_blake2b(
        h0.data(), sizeof(h0), input.data(), input.size(), nullptr, 0);
    if (res != 0) {
      throw std::runtime_error("blake2b hash failed");
    }

    for (size_t j = 0; j < m; j++) {
      res = crypto_generichash_blake2b_salt_personal(
          output.data() + (crypto_generichash_blake2b_BYTES_MAX * j),
          crypto_generichash_blake2b_BYTES_MAX,
          h0.data(),
          h0.size(),
          nullptr /* key */,
          0 /* keylen */,
          nullptr /* salt */,
          personalization.data());
      if (res != 0) {
        throw std::runtime_error("blake2b hash failed");
      }
      sodium_increment(
          personalization.data(), crypto_generichash_blake2b_PERSONALBYTES);
    }
  }
}

void benchmarkBlake2xb(size_t inputSize, size_t outputSize, size_t n) {
  std::vector<uint8_t> input;
  std::vector<uint8_t> output;
  BENCHMARK_SUSPEND {
    input.resize(inputSize);
    output.resize(outputSize);
  };
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    Blake2xb::hash({output.data(), output.size()}, folly::range(input));
  }
}

BENCHMARK(blake2b_100b_in_64b_out, n) {
  benchmarkBlake2b(100, n);
}

BENCHMARK_RELATIVE(blake2xb_100b_in_64b_out, n) {
  benchmarkBlake2xb(100, 64, n);
}

BENCHMARK(blake2b_100b_in_128b_out, n) {
  benchmarkBlake2bMultiple(100, 128 / 64, n);
}

BENCHMARK_RELATIVE(blake2xb_100b_in_128b_out, n) {
  benchmarkBlake2xb(100, 128, n);
}

BENCHMARK(blake2b_100b_in_1024b_out, n) {
  benchmarkBlake2bMultiple(100, 1024 / 64, n);
}

BENCHMARK_RELATIVE(blake2xb_100b_in_1024b_out, n) {
  benchmarkBlake2xb(100, 1024, n);
}

BENCHMARK(blake2b_100b_in_4096b_out, n) {
  benchmarkBlake2bMultiple(100, 4096 / 64, n);
}

BENCHMARK_RELATIVE(blake2xb_100b_in_4096b_out, n) {
  benchmarkBlake2xb(100, 4096, n);
}

BENCHMARK(blake2b_1000b_in_64b_out, n) {
  benchmarkBlake2b(1000, n);
}

BENCHMARK_RELATIVE(blake2xb_1000b_in_64b_out, n) {
  benchmarkBlake2xb(1000, 64, n);
}

BENCHMARK(blake2b_1000b_in_128b_out, n) {
  benchmarkBlake2bMultiple(1000, 128 / 64, n);
}

BENCHMARK_RELATIVE(blake2xb_1000b_in_128b_out, n) {
  benchmarkBlake2xb(1000, 128, n);
}

BENCHMARK(blake2b_1000b_in_1024b_out, n) {
  benchmarkBlake2bMultiple(1000, 1024 / 64, n);
}

BENCHMARK_RELATIVE(blake2xb_1000b_in_1024b_out, n) {
  benchmarkBlake2xb(1000, 1024, n);
}

BENCHMARK(blake2b_1000b_in_4096b_out, n) {
  benchmarkBlake2bMultiple(1000, 4096 / 64, n);
}

BENCHMARK_RELATIVE(blake2xb_1000b_in_4096b_out, n) {
  benchmarkBlake2xb(1000, 4096, n);
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
