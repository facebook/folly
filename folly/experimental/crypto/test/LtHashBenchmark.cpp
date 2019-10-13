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

#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/experimental/crypto/LtHash.h>
#include <folly/init/Init.h>
#include <folly/io/IOBuf.h>
#include <glog/logging.h>
#include <sodium.h>

using namespace ::folly::crypto;

namespace {
constexpr size_t kObjectCount = 1000;
constexpr size_t kObjectSize = 150;
std::vector<std::unique_ptr<const folly::IOBuf>> kObjects;
} // namespace

std::unique_ptr<folly::IOBuf> makeRandomData(size_t length) {
  auto data = std::make_unique<folly::IOBuf>(
      folly::crypto::detail::allocateCacheAlignedIOBuf(length));
  data->append(length);
  randombytes_buf(data->writableData(), data->length());
  return data;
}

template <std::size_t B, std::size_t N>
void runBenchmark(size_t n) {
  LtHash<B, N> ltHash;
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    const folly::IOBuf& obj = *(kObjects[i % kObjects.size()]);
    ltHash.addObject({obj.data(), obj.length()});
  }
}

BENCHMARK(single_blake2b, n) {
  std::array<unsigned char, crypto_generichash_blake2b_BYTES_MAX> result;
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    const folly::IOBuf& obj = *(kObjects[i % kObjects.size()]);
    int res = crypto_generichash_blake2b(
        result.data(), sizeof(result), obj.data(), obj.length(), nullptr, 0);
    if (res != 0) {
      throw std::runtime_error("blake2b hash failed");
    }
  }
}

BENCHMARK_RELATIVE(LtHash_element_count_1024_length_16, n) {
  runBenchmark<16, 1024>(static_cast<size_t>(n));
}

BENCHMARK_RELATIVE(LtHash_element_count_1008_length_20, n) {
  runBenchmark<20, 1008>(static_cast<size_t>(n));
}

BENCHMARK_RELATIVE(LtHash_element_count_1024_length_32, n) {
  runBenchmark<32, 1024>(static_cast<size_t>(n));
}

BENCHMARK_RELATIVE(LtHash_element_count_2048_length_32, n) {
  runBenchmark<32, 2048>(static_cast<size_t>(n));
}

BENCHMARK(calculateChecksumFor100KObjects_B20_N1008) {
  LtHash<20, 1008> ltHash;
  for (auto i = 0; i < 100000; ++i) {
    const folly::IOBuf& obj = *(kObjects[i % kObjects.size()]);
    ltHash.addObject({obj.data(), obj.length()});
  }
}

BENCHMARK_RELATIVE(calculateChecksumFor100KObjects_B16_N1024) {
  LtHash<16, 1024> ltHash;
  for (auto i = 0; i < 100000; ++i) {
    const folly::IOBuf& obj = *(kObjects[i % kObjects.size()]);
    ltHash.addObject({obj.data(), obj.length()});
  }
}

BENCHMARK_RELATIVE(calculateChecksumFor100KObjects_B32_N1024) {
  LtHash<32, 1024> ltHash;
  for (auto i = 0; i < 100000; ++i) {
    const folly::IOBuf& obj = *(kObjects[i % kObjects.size()]);
    ltHash.addObject({obj.data(), obj.length()});
  }
}

BENCHMARK(subtractChecksumFor100KObjects_B20_N1008) {
  LtHash<20, 1008> ltHash;
  for (auto i = 0; i < 100000; ++i) {
    const folly::IOBuf& obj = *(kObjects[i % kObjects.size()]);
    ltHash.removeObject({obj.data(), obj.length()});
  }
}

BENCHMARK_RELATIVE(subtractChecksumFor100KObjects_B16_N1024) {
  LtHash<16, 1024> ltHash;
  for (auto i = 0; i < 100000; ++i) {
    const folly::IOBuf& obj = *(kObjects[i % kObjects.size()]);
    ltHash.removeObject({obj.data(), obj.length()});
  }
}

BENCHMARK_RELATIVE(subtractChecksumFor100KObjects_B32_N1024) {
  LtHash<32, 1024> ltHash;
  for (auto i = 0; i < 100000; ++i) {
    const folly::IOBuf& obj = *(kObjects[i % kObjects.size()]);
    ltHash.removeObject({obj.data(), obj.length()});
  }
}

int main(int argc, char** argv) {
  folly::init(&argc, &argv);

  if (sodium_init() < 0) {
    throw std::runtime_error("Failed to initialize libsodium");
  }

  // pre-generate objects with random length to hash
  for (size_t i = 0; i < kObjectCount; i++) {
    kObjects.push_back(makeRandomData(kObjectSize));
  }

  // Trigger the implementation selection of AUTO math operations before
  // starting the benchmark, so log messages don't pollute the output table.
  LtHash<20, 1008> ltHash;
  ltHash.addObject(folly::range("hello world"));
  ltHash.removeObject(folly::range("hello world"));

  folly::runBenchmarks();
  return 0;
}
