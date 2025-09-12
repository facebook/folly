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

#include <folly/synchronization/StripedThrottledLifoSem.h>

#include <thread>

#include <folly/portability/GFlags.h>
#include <folly/portability/SysResource.h>
#include <folly/system/ThreadName.h>
#include <folly/tracing/StaticTracepoint.h>

DEFINE_uint64(
    folly_striped_throttled_lifo_sem_balancer_period_us,
    100'000,
    "How frequently StripedThrottledLifoSem load balancing is run");
DEFINE_uint64(
    folly_striped_throttled_lifo_sem_balancer_backoff_us,
    1'000,
    "When an iteration of StripedThrottledLifoSem load balancing actually "
    "results in balancing operations, run the next iteration after this delay "
    "instead of a whole period");

namespace folly {

class StripedThrottledLifoSemBalancer::Worker {
 public:
  explicit Worker(StripedThrottledLifoSemBalancer& parent)
      : parent_(parent), thread_{[this] { loop(); }} {}

  ~Worker() {
    {
      std::lock_guard g(parent_.mutex_);
      shutdown_ = true;
    }
    parent_.cv_.notify_all();
    thread_.join();
  }

  void loop() {
    setThreadName("STLSBalancer");
    // Run this thread at the lowest priority. The purpose is to ensure that
    // there are no underutilized threads, so if the CPU is already
    // oversubscribed it is fine to delay the iteration. This is best effort,
    // not checking success.
    setpriority(PRIO_PROCESS, 0, 19);

    bool didTransfer = false;
    while (true) {
      std::unique_lock g(parent_.mutex_);
      std::chrono::microseconds waitTime{
          didTransfer
              ? FLAGS_folly_striped_throttled_lifo_sem_balancer_backoff_us
              : FLAGS_folly_striped_throttled_lifo_sem_balancer_period_us};
      if (parent_.cv_.wait_for(g, waitTime, [this] { return shutdown_; })) {
        break;
      }

      didTransfer = false;
      for (const auto& [semPtr, balanceStep] : parent_.sems_) {
        auto transfers = balanceStep();
        FOLLY_SDT(
            folly, striped_throttled_lifo_sem_balancer_step, semPtr, transfers);
        didTransfer = didTransfer || transfers > 0;
        parent_.totalTransfers_ += transfers;
      }
    }
  }

 private:
  Worker(const Worker&) = delete;
  Worker(Worker&&) = delete;
  Worker& operator=(const Worker&) = delete;
  Worker& operator=(Worker&&) = delete;

  StripedThrottledLifoSemBalancer& parent_;
  std::thread thread_;
  bool shutdown_ = false;
};

/* static */ StripedThrottledLifoSemBalancer&
StripedThrottledLifoSemBalancer::instance() {
  static folly::Indestructible<StripedThrottledLifoSemBalancer> ret{
      PrivateTag{}};
  return ret;
}

/* static */ void StripedThrottledLifoSemBalancer::ensureWorker() {
  static Worker worker{instance()};
}

} // namespace folly
