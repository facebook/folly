/*
 * Copyright 2016-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/Benchmark.h>
#include <folly/io/IOBuf.h>

using folly::IOBuf;

BENCHMARK(cloneOneBenchmark, iters) {
  IOBuf buf(IOBuf::CREATE, 10);
  while (iters--) {
    auto copy = buf.cloneOne();
    folly::doNotOptimizeAway(copy->capacity());
  }
}

BENCHMARK(cloneOneIntoBenchmark, iters) {
  IOBuf buf(IOBuf::CREATE, 10);
  IOBuf copy;
  while (iters--) {
    buf.cloneOneInto(copy);
    folly::doNotOptimizeAway(copy.capacity());
  }
}

BENCHMARK(cloneBenchmark, iters) {
  IOBuf buf(IOBuf::CREATE, 10);
  while (iters--) {
    auto copy = buf.clone();
    folly::doNotOptimizeAway(copy->capacity());
  }
}

BENCHMARK(cloneIntoBenchmark, iters) {
  IOBuf buf(IOBuf::CREATE, 10);
  IOBuf copy;
  while (iters--) {
    buf.cloneInto(copy);
    folly::doNotOptimizeAway(copy.capacity());
  }
}

BENCHMARK(moveBenchmark, iters) {
  IOBuf buf(IOBuf::CREATE, 10);
  while (iters--) {
    auto tmp = std::move(buf);
    folly::doNotOptimizeAway(tmp.capacity());
    buf = std::move(tmp);
  }
}

BENCHMARK(copyBenchmark, iters) {
  IOBuf buf(IOBuf::CREATE, 10);
  while (iters--) {
    auto copy = buf;
    folly::doNotOptimizeAway(copy.capacity());
  }
}

BENCHMARK(cloneCoalescedBaseline, iters) {
  std::unique_ptr<IOBuf> buf = IOBuf::createChain(100, 10);
  while (iters--) {
    auto clone = buf->cloneAsValue();
    clone.coalesce();
    folly::doNotOptimizeAway(clone.capacity());
  }
}

BENCHMARK_RELATIVE(cloneCoalescedBenchmark, iters) {
  std::unique_ptr<IOBuf> buf = IOBuf::createChain(100, 10);
  while (iters--) {
    auto copy = buf->cloneCoalescedAsValue();
    folly::doNotOptimizeAway(copy.capacity());
  }
}

BENCHMARK(takeOwnershipBenchmark, iters) {
  size_t data = 0;
  while (iters--) {
    std::unique_ptr<IOBuf> buf(IOBuf::takeOwnership(
        &data,
        sizeof(data),
        [](void* /*unused*/, void* /*unused*/) {},
        nullptr));
  }
}

/**
 * ============================================================================
 * folly/io/test/IOBufBenchmark.cpp                relative  time/iter  iters/s
 * ============================================================================
 * cloneOneBenchmark                                           39.97ns   25.02M
 * cloneOneIntoBenchmark                                       21.33ns   46.88M
 * cloneBenchmark                                              33.80ns   29.59M
 * cloneIntoBenchmark                                          21.81ns   45.86M
 * moveBenchmark                                                9.26ns  108.03M
 * copyBenchmark                                               27.10ns   36.91M
 * cloneCoalescedBaseline                                     305.29ns    3.28M
 * cloneCoalescedBenchmark                          676.51%    45.13ns   22.16M
 * takeOwnershipBenchmark                                      43.04ns   23.23M
 * ============================================================================
 */

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
