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

#include <folly/executors/SoftRealTimeExecutor.h>

#include <atomic>

#include <glog/logging.h>

namespace folly {

namespace {

class DeadlineExecutor : public folly::Executor {
 public:
  static KeepAlive<> create(
      uint64_t deadline, KeepAlive<SoftRealTimeExecutor> executor) {
    return makeKeepAlive(new DeadlineExecutor(deadline, std::move(executor)));
  }

  void add(folly::Func f) override { executor_->add(std::move(f), deadline_); }

  bool keepAliveAcquire() noexcept override {
    const auto count = keepAliveCount_.fetch_add(1, std::memory_order_relaxed);
    DCHECK_GT(count, 0);
    return true;
  }

  void keepAliveRelease() noexcept override {
    const auto count = keepAliveCount_.fetch_sub(1, std::memory_order_acq_rel);
    DCHECK_GT(count, 0);
    if (count == 1) {
      delete this;
    }
  }

 private:
  DeadlineExecutor(uint64_t deadline, KeepAlive<SoftRealTimeExecutor> executor)
      : deadline_(deadline), executor_(std::move(executor)) {}

  std::atomic<size_t> keepAliveCount_{1};
  uint64_t deadline_;
  KeepAlive<SoftRealTimeExecutor> executor_;
};

} // namespace

folly::Executor::KeepAlive<> SoftRealTimeExecutor::deadlineExecutor(
    uint64_t deadline) {
  return DeadlineExecutor::create(deadline, getKeepAliveToken(this));
}

} // namespace folly
