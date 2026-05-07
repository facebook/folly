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

#pragma once

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/SoftRealTimeExecutor.h>
#include <folly/synchronization/ThrottledLifoSem.h>

namespace folly {

/**
 * An approximate implementation of an Earliest Deadline First executor.
 *
 * Instead of having a global priority queue, we maintain one independent queue
 * for each LLC cache, to avoid expensive cross-LLC traffic. This implies that
 * the EDF policy is only honored among tasks submitted from CPUs sharing the
 * same LLC. In practice, each LLC should have enough CPUs to make the
 * approximation good enough for most use cases.
 *
 * Within a single stripe, deadline ties are broken by submission order: of two
 * tasks with the same deadline submitted to the same stripe, the one submitted
 * first is dequeued first. There is no equivalent guarantee across stripes,
 * because tracking a global submission order would require synchronization
 * between stripes, defeating the purpose of striping.
 */
class StripedEDFThreadPoolExecutor
    : public SoftRealTimeExecutor,
      public CPUThreadPoolExecutor {
 public:
  struct Options {
    ThrottledLifoSem::Options tlsOptions;

    // If true, force a single stripe regardless of the CPU topology. This
    // gives up the per-LLC scalability benefit, but in exchange the executor
    // honors a strict global EDF policy with submission-order tie-breaking,
    // making it equivalent to folly::EDFThreadPoolExecutor.
    bool strictOrdering = false;
  };

  static constexpr uint64_t kEarliestDeadline = 0;
  static constexpr uint64_t kLatestDeadline =
      std::numeric_limits<uint64_t>::max();

  explicit StripedEDFThreadPoolExecutor(
      size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("StripedEDFTP"),
      const Options& options = defaultOptions())
      : StripedEDFThreadPoolExecutor(
            {numThreads, numThreads}, std::move(threadFactory), options) {}

  explicit StripedEDFThreadPoolExecutor(
      std::pair<size_t, size_t> numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("StripedEDFTP"),
      const Options& options = defaultOptions());

  using CPUThreadPoolExecutor::add;

  // SoftRealTimeExecutor
  void add(Func f, uint64_t deadline) override;
  void add(std::vector<Func> fs, uint64_t deadline) override;

 private:
  static Options defaultOptions() { return {}; }
};

} // namespace folly
