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

#include <algorithm>
#include <atomic>
#include <cstring>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include <glog/logging.h>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <folly/io/async/IoUringProvidedBufferRing.h>
#include <folly/portability/Asm.h>

#if FOLLY_HAS_LIBURING

namespace {

constexpr uint32_t kBufferCount = 1024;
constexpr uint32_t kBufferSize = 4096;
constexpr size_t kPageSize = 4096;
constexpr size_t kCacheLineSize = 64;

struct RingFixture {
  struct io_uring ring{};
  folly::IoUringProvidedBufferRing::UniquePtr provider;

  explicit RingFixture(
      uint32_t bufferCount, uint32_t bufferSize, bool useIncremental = false) {
    int ret = io_uring_queue_init(64, &ring, 0);
    if (ret < 0) {
      throw std::runtime_error("io_uring_queue_init failed");
    }

    folly::IoUringProvidedBufferRing::Options opts;
    opts.gid = 1;
    opts.bufferCount = bufferCount;
    opts.bufferSize = bufferSize;
    opts.useIncrementalBuffers = useIncremental;
    provider = folly::IoUringProvidedBufferRing::create(&ring, opts);

    constexpr uint32_t kWarmupCycles = 4;
    uint16_t warmBid = 0;
    for (uint32_t i = 0; i < bufferCount * kWarmupCycles; ++i) {
      auto iobuf = provider->getIoBuf(warmBid, bufferSize, false);
      uint8_t* data = iobuf->writableData();
      for (size_t off = 0; off < iobuf->length(); off += kPageSize) {
        data[off] = static_cast<uint8_t>(off + i);
      }
      iobuf.reset();
      warmBid = (warmBid + 1) % bufferCount;
    }
  }

  ~RingFixture() {
    provider.reset();
    io_uring_queue_exit(&ring);
  }
};

uint64_t consumeData(const folly::IOBuf& buf) {
  uint64_t sum = 0;
  for (auto range : buf) {
    const auto* base = range.data();
    for (size_t off = 0; off < range.size(); off += kCacheLineSize) {
      uint64_t word;
      std::memcpy(&word, base + off, sizeof(word));
      sum += word;
    }
  }
  return sum;
}

// Returns the sequence of buffer ids to walk. When shuffled, the ids are
// permuted so consecutive iterations touch non-adjacent buffers (defeats
// prefetch / spatial locality) while covering the same set exactly once.
std::vector<uint16_t> makeBids(uint32_t count, bool shuffled) {
  std::vector<uint16_t> bids(count);
  std::iota(bids.begin(), bids.end(), 0);
  if (shuffled) {
    constexpr uint64_t kSeed = 42;
    std::mt19937 rng(kSeed);
    std::shuffle(bids.begin(), bids.end(), rng);
  }
  return bids;
}

void benchGetIoBufSingleImpl(
    uint32_t iters, uint32_t bufferSize, bool shuffled) {
  folly::BenchmarkSuspender setup;
  RingFixture fixture(kBufferCount, bufferSize);
  auto bids = makeBids(kBufferCount, shuffled);
  setup.dismiss();

  uint32_t idx = 0;
  for (uint32_t i = 0; i < iters; ++i) {
    auto iobuf = fixture.provider->getIoBuf(bids[idx], bufferSize, false);
    folly::doNotOptimizeAway(consumeData(*iobuf));
    iobuf.reset();
    idx = (idx + 1) % kBufferCount;
  }
}

void benchGetIoBufSingle(uint32_t iters, uint32_t bufferSize) {
  benchGetIoBufSingleImpl(iters, bufferSize, false);
}

void benchGetIoBufShuffled(uint32_t iters, uint32_t bufferSize) {
  benchGetIoBufSingleImpl(iters, bufferSize, true);
}

void benchGetIoBufBatchImpl(uint32_t iters, uint32_t batchSize, bool shuffled) {
  folly::BenchmarkSuspender setup;
  RingFixture fixture(kBufferCount, kBufferSize);
  auto bids = makeBids(kBufferCount, shuffled);
  std::vector<std::unique_ptr<folly::IOBuf>> batch(batchSize);
  setup.dismiss();

  uint32_t idx = 0;
  for (uint32_t i = 0; i < iters; ++i) {
    for (uint32_t j = 0; j < batchSize; ++j) {
      batch[j] = fixture.provider->getIoBuf(bids[idx], kBufferSize, false);
      folly::doNotOptimizeAway(consumeData(*batch[j]));
      idx = (idx + 1) % kBufferCount;
    }
    for (uint32_t j = 0; j < batchSize; ++j) {
      batch[j].reset();
    }
  }
}

void benchGetIoBufBatch(uint32_t iters, uint32_t batchSize) {
  benchGetIoBufBatchImpl(iters, batchSize, false);
}

void benchGetIoBufBatchShuffled(uint32_t iters, uint32_t batchSize) {
  benchGetIoBufBatchImpl(iters, batchSize, true);
}

void benchGetIoBufChain(uint32_t iters, uint32_t chainLength) {
  folly::BenchmarkSuspender setup;
  RingFixture fixture(kBufferCount, kBufferSize);
  setup.dismiss();

  uint16_t bid = 0;
  size_t totalLength = static_cast<size_t>(chainLength) * kBufferSize;
  for (uint32_t i = 0; i < iters; ++i) {
    auto chain = fixture.provider->getIoBuf(bid, totalLength, false);
    folly::doNotOptimizeAway(consumeData(*chain));
    chain.reset();
    bid = (bid + chainLength) % kBufferCount;
  }
}

constexpr uint32_t kReturnBatch = 128;

void benchReturnBufferMultiThread(uint32_t iters, uint32_t numThreads) {
  folly::BenchmarkSuspender setup;
  RingFixture fixture(kBufferCount, kBufferSize);
  std::vector<std::vector<std::unique_ptr<folly::IOBuf>>> perThread(numThreads);

  std::atomic<uint32_t> generation{0};
  std::atomic<uint32_t> completed{0};
  std::atomic<bool> stop{false};

  std::vector<std::thread> threads;
  threads.reserve(numThreads);
  for (uint32_t t = 0; t < numThreads; ++t) {
    threads.emplace_back([&perThread, &generation, &completed, &stop, t] {
      uint32_t localGen = 0;
      while (true) {
        ++localGen;
        while (generation.load(std::memory_order_acquire) < localGen) {
          if (stop.load(std::memory_order_acquire)) {
            return;
          }
          folly::asm_volatile_pause();
        }
        perThread[t].clear();
        completed.fetch_add(1, std::memory_order_release);
      }
    });
  }
  setup.dismiss();

  uint16_t bid = 0;
  for (uint32_t i = 0; i < iters; ++i) {
    {
      folly::BenchmarkSuspender acquire;
      for (uint32_t j = 0; j < kReturnBatch; ++j) {
        perThread[bid % numThreads].push_back(
            fixture.provider->getIoBuf(bid, kBufferSize, false));
        bid = (bid + 1) % kBufferCount;
      }
    }

    completed.store(0, std::memory_order_release);
    generation.fetch_add(1, std::memory_order_release);
    while (completed.load(std::memory_order_acquire) < numThreads) {
      folly::asm_volatile_pause();
    }
  }

  stop.store(true, std::memory_order_release);
  for (auto& th : threads) {
    th.join();
  }
}

void benchGetIoBufIncremental(uint32_t iters, uint32_t chunksPerBuffer) {
  folly::BenchmarkSuspender setup;
  RingFixture fixture(kBufferCount, kBufferSize, true);
  CHECK_EQ(kBufferSize % chunksPerBuffer, 0u)
      << "chunksPerBuffer must divide kBufferSize evenly so each iteration "
         "touches the full buffer";
  const size_t chunkSize = kBufferSize / chunksPerBuffer;
  std::vector<std::unique_ptr<folly::IOBuf>> chunks(chunksPerBuffer);
  setup.dismiss();

  uint16_t bid = 0;
  for (uint32_t i = 0; i < iters; ++i) {
    for (uint32_t c = 0; c < chunksPerBuffer; ++c) {
      bool hasMore = (c + 1 < chunksPerBuffer);
      chunks[c] = fixture.provider->getIoBuf(bid, chunkSize, hasMore);
      folly::doNotOptimizeAway(consumeData(*chunks[c]));
    }
    for (uint32_t c = 0; c < chunksPerBuffer; ++c) {
      chunks[c].reset();
    }
    bid = (bid + 1) % kBufferCount;
  }
}

} // namespace

BENCHMARK_NAMED_PARAM(benchGetIoBufSingle, 64B, 64)
BENCHMARK_NAMED_PARAM(benchGetIoBufSingle, 4KB, 4096)
BENCHMARK_NAMED_PARAM(benchGetIoBufSingle, 64KB, 65536)
BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(benchGetIoBufBatch, batch_1, 1)
BENCHMARK_NAMED_PARAM(benchGetIoBufBatch, batch_8, 8)
BENCHMARK_NAMED_PARAM(benchGetIoBufBatch, batch_32, 32)
BENCHMARK_NAMED_PARAM(benchGetIoBufBatch, batch_128, 128)
BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(benchGetIoBufChain, chain_2, 2)
BENCHMARK_NAMED_PARAM(benchGetIoBufChain, chain_4, 4)
BENCHMARK_NAMED_PARAM(benchGetIoBufChain, chain_8, 8)
BENCHMARK_NAMED_PARAM(benchGetIoBufChain, chain_16, 16)
BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(benchGetIoBufSingle, seq_4KB, 4096)
BENCHMARK_NAMED_PARAM(benchGetIoBufShuffled, shuf_4KB, 4096)
BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(benchGetIoBufSingle, seq_64KB, 65536)
BENCHMARK_NAMED_PARAM(benchGetIoBufShuffled, shuf_64KB, 65536)
BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(benchGetIoBufBatch, seq_batch_32, 32)
BENCHMARK_NAMED_PARAM(benchGetIoBufBatchShuffled, shuf_batch_32, 32)
BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(benchReturnBufferMultiThread, 2_threads, 2)
BENCHMARK_NAMED_PARAM(benchReturnBufferMultiThread, 4_threads, 4)
BENCHMARK_NAMED_PARAM(benchReturnBufferMultiThread, 8_threads, 8)
BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(benchGetIoBufIncremental, inc_4_chunks, 4)
BENCHMARK_NAMED_PARAM(benchGetIoBufIncremental, inc_8_chunks, 8)
BENCHMARK_NAMED_PARAM(benchGetIoBufIncremental, inc_16_chunks, 16)
BENCHMARK_DRAW_LINE();

#endif

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
