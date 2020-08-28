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

#include <sys/eventfd.h>

#include <folly/Benchmark.h>
#include <folly/FileUtil.h>
#include <folly/experimental/io/AsyncIO.h>
#include <folly/experimental/io/IoUring.h>
#include <folly/experimental/io/test/AsyncBaseTestLib.h>
#include <folly/experimental/io/test/IoTestTempFileUtil.h>
#include <folly/portability/GFlags.h>

namespace {

static constexpr size_t kBlockSize = 4096;
static constexpr size_t kNumBlocks = 4096;

static folly::test::TemporaryFile& getTempFile(size_t num) {
  CHECK_LE(num, kNumBlocks);

  static auto sTempFile =
      folly::test::TempFileUtil::getTempFile(kNumBlocks * kBlockSize);

  return sTempFile;
}

template <typename OP>
struct BenchmarkData {
  BenchmarkData(size_t num, size_t& c) : numEntries(num), completed(c) {
    fd = ::open(getTempFile(num).path().c_str(), O_DIRECT | O_RDONLY);
    CHECK_GE(fd, 0);
    ops.reserve(numEntries);
    bufs.reserve(numEntries);
    for (size_t i = 0; i < numEntries; i++) {
      bufs.push_back(
          folly::test::async_base_test_lib_detail::TestUtil::allocateAligned(
              kBlockSize));
    }
  }

  ~BenchmarkData() {
    ::close(fd);
  }

  void reset() {
    ops.clear();
    for (size_t i = 0; i < numEntries; i++) {
      ops.push_back(std::make_unique<OP>());
      auto& op = *ops.back();
      op.setNotificationCallback([&](folly::AsyncBaseOp*) { ++completed; });
      op.pread(fd, bufs[i].get(), kBlockSize, i * kBlockSize);
    }
  }

  std::vector<std::unique_ptr<folly::AsyncBase::Op>> ops;
  std::vector<folly::test::async_base_test_lib_detail::TestUtil::ManagedBuffer>
      bufs;
  size_t numEntries;
  size_t& completed;
  int fd{-1};
};

template <typename TAsync>
void runTAsyncIOTest(
    unsigned int iters,
    size_t numEntries,
    size_t batchSize,
    bool persist) {
  folly::BenchmarkSuspender suspender;
  std::vector<folly::AsyncBase::Op*> ops;
  ops.reserve(batchSize);
  std::unique_ptr<TAsync> aio(
      persist
          ? new TAsync(numEntries, folly::AsyncBase::NOT_POLLABLE, batchSize)
          : nullptr);
  size_t completed = 0;
  BenchmarkData<typename TAsync::Op> bmData(numEntries, completed);
  suspender.dismiss();
  for (unsigned iter = 0; iter < iters; iter++) {
    if (!persist) {
      aio.reset(
          new TAsync(numEntries, folly::AsyncBase::NOT_POLLABLE, batchSize));
    }
    completed = 0;
    bmData.reset();
    size_t num = 0;
    for (size_t i = 0; i < numEntries; i++) {
      ops.push_back(bmData.ops[i].get());
      if (++num == batchSize) {
        num = 0;
        aio->submit(folly::Range(ops.data(), ops.data() + ops.size()));
        ops.clear();
      }
    }
    if (num) {
      aio->submit(folly::Range(ops.data(), ops.data() + ops.size()));
      ops.clear();
    }
    aio->wait(numEntries);
    CHECK_EQ(completed, numEntries);
    if (!persist) {
      aio.reset();
    }
  }
  aio.reset();
  suspender.rehire();
}

void runAsyncIOTest(
    unsigned int iters,
    size_t numEntries,
    size_t batchSize,
    bool persist) {
  class BatchAsyncIO : public folly::AsyncIO {
   public:
    BatchAsyncIO(size_t capacity, PollMode pollMode, size_t /*unused*/)
        : folly::AsyncIO(capacity, pollMode) {}
  };
  runTAsyncIOTest<BatchAsyncIO>(iters, numEntries, batchSize, persist);
}

void runIOUringTest(
    unsigned int iters,
    size_t numEntries,
    size_t batchSize,
    bool persist) {
  class BatchIoUring : public folly::IoUring {
   public:
    BatchIoUring(size_t capacity, PollMode pollMode, size_t batchSize)
        : folly::IoUring(capacity, pollMode, batchSize) {}
  };

  runTAsyncIOTest<BatchIoUring>(iters, numEntries, batchSize, persist);
}

} // namespace
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(
    runAsyncIOTest,
    async_io_no_batching_no_persist,
    4096,
    1,
    false)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runAsyncIOTest,
    async_io_batching_64_no_persist,
    4096,
    64,
    false)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runAsyncIOTest,
    async_io_batching_256_no_persist,
    4096,
    256,
    false)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runAsyncIOTest,
    async_io_no_batching_persist,
    4096,
    1,
    true)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runAsyncIOTest,
    async_io_batching_64_persist,
    4096,
    64,
    true)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runAsyncIOTest,
    async_io_batching_256_persist,
    4096,
    256,
    true)
BENCHMARK_DRAW_LINE();
BENCHMARK_RELATIVE_NAMED_PARAM(
    runIOUringTest,
    io_uring_no_batching_no_persist,
    4096,
    1,
    false)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runIOUringTest,
    io_uring_batching_64_no_persist,
    4096,
    64,
    false)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runIOUringTest,
    io_uring_batching_256_no_persist,
    4096,
    256,
    false)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runIOUringTest,
    io_uring_no_batching_persist,
    4096,
    1,
    true)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runIOUringTest,
    io_uring_batching_64_persist,
    4096,
    64,
    true)
BENCHMARK_RELATIVE_NAMED_PARAM(
    runIOUringTest,
    io_uring_batching_256_persist,
    4096,
    256,
    true)
BENCHMARK_DRAW_LINE();

int main(int argc, char** argv) {
  getTempFile(kNumBlocks);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
}

/*
./io_benchmark --bm_min_iters=100
============================================================================
folly/experimental/io/test/IOBenchmark.cpp      relative  time/iter  iters/s
============================================================================
----------------------------------------------------------------------------
runAsyncIOTest(async_io_no_batching_no_persist)            111.61ms     8.96
runAsyncIOTest(async_io_batching_64_no_persist)   99.89%   111.73ms     8.95
runAsyncIOTest(async_io_batching_256_no_persist   97.35%   114.65ms     8.72
runAsyncIOTest(async_io_no_batching_persist)     120.09%    92.94ms    10.76
runAsyncIOTest(async_io_batching_64_persist)     119.98%    93.03ms    10.75
runAsyncIOTest(async_io_batching_256_persist)    117.25%    95.19ms    10.51
----------------------------------------------------------------------------
runIOUringTest(io_uring_no_batching_no_persist)  120.59%    92.55ms    10.80
runIOUringTest(io_uring_batching_64_no_persist)  120.62%    92.53ms    10.81
runIOUringTest(io_uring_batching_256_no_persist  120.52%    92.61ms    10.80
runIOUringTest(io_uring_no_batching_persist)     116.74%    95.61ms    10.46
runIOUringTest(io_uring_batching_64_persist)     120.64%    92.52ms    10.81
runIOUringTest(io_uring_batching_256_persist)    120.31%    92.77ms    10.78
----------------------------------------------------------------------------
============================================================================
*/
