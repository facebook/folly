/*
 * Copyright 2016 Facebook, Inc.
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
#include <folly/SmallLocks.h>

BENCHMARK(MicroSpinLockUncontendedBenchmark, iters) {
  folly::MicroSpinLock lock;
  lock.init();
  while (iters--) {
    lock.lock();
    lock.unlock();
  }
}

BENCHMARK(PicoSpinLockUncontendedBenchmark, iters) {
  // uint8_t would be more fair, but PicoSpinLock needs at lesat two bytes
  folly::PicoSpinLock<uint16_t> lock;
  lock.init();
  while (iters--) {
    lock.lock();
    lock.unlock();
  }
}

BENCHMARK(MicroLockUncontendedBenchmark, iters) {
  folly::MicroLock lock;
  lock.init();
  while (iters--) {
    lock.lock();
    lock.unlock();
  }
}

BENCHMARK(StdMutexUncontendedBenchmark, iters) {
  std::mutex lock;
  while (iters--) {
    lock.lock();
    lock.unlock();
  }
}

struct VirtualBase {
  virtual void foo() = 0;
  virtual ~VirtualBase() {}
};

struct VirtualImpl : VirtualBase {
  void foo() override { /* noop */
  }
  virtual ~VirtualImpl() {}
};

#ifndef __clang__
__attribute__((noinline, noclone)) VirtualBase* makeVirtual() {
  return new VirtualImpl();
}

BENCHMARK(VirtualFunctionCall, iters) {
  VirtualBase* vb = makeVirtual();
  while (iters--) {
    vb->foo();
  }
  delete vb;
}
#endif

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
