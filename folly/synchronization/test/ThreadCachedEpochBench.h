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

#include <folly/Benchmark.h>
#include <folly/synchronization/detail/ThreadCachedReaders.h>

template <typename ThreadCachedEpoch>
void bm_increment_loop(unsigned iters) {
  folly::BenchmarkSuspender susp;

  ThreadCachedEpoch cachedEpoch;
  cachedEpoch.increment(0);
  cachedEpoch.decrement();

  susp.dismiss();

  for (unsigned i = 0; i < iters; i++) {
    cachedEpoch.increment(0);
  }
}

template <typename ThreadCachedEpoch>
void bm_increment_decrement_same_loop(unsigned iters) {
  folly::BenchmarkSuspender susp;

  ThreadCachedEpoch cachedEpoch;
  cachedEpoch.increment(0);
  cachedEpoch.decrement();

  susp.dismiss();

  for (unsigned i = 0; i < iters; i++) {
    cachedEpoch.increment(0);
    cachedEpoch.decrement();
  }
}

template <typename ThreadCachedEpoch>
void bm_increment_decrement_separate_loop(unsigned iters) {
  folly::BenchmarkSuspender susp;

  ThreadCachedEpoch cachedEpoch;
  cachedEpoch.increment(0);
  cachedEpoch.decrement();

  susp.dismiss();

  for (unsigned i = 0; i < iters; i++) {
    cachedEpoch.increment(0);
  }
  for (unsigned i = 0; i < iters; i++) {
    cachedEpoch.decrement();
  }
}
