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

#include <atomic>
#include <memory>
#include <mutex>

#include <folly/concurrency/UnboundedQueue.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/executors/SerializedExecutor.h>
#include <folly/io/async/Request.h>
#include <folly/synchronization/DistributedMutex.h>
#include <folly/synchronization/RelaxedAtomic.h>

namespace folly {

namespace detail {

/**
 * @class SerialExecutor
 *
 * @brief Executor that guarantees serial non-concurrent execution of added
 *     tasks
 *
 * SerialExecutor is similar to boost asio's strand concept. A SerialExecutor
 * has a parent executor which is given at construction time (defaults to
 * folly's global CPUExecutor). Tasks added to SerialExecutor are executed
 * in the parent executor, however strictly non-concurrently and in the order
 * they were added.
 *
 * When a task is added to the executor while another one is running on the
 * parent executor, the new task is piggybacked on the running task to save the
 * cost of scheduling a task on the parent executor. This implies that the
 * parent executor may observe a smaller number of tasks than those added in the
 * SerialExecutor.
 *
 * The SerialExecutor may be deleted at any time. All tasks that have been
 * submitted will still be executed with the same guarantees, as long as the
 * parent executor is executing tasks.
 */
template <template <typename> typename Queue>
class SerialExecutorImpl : public SerializedExecutor {
 public:
  SerialExecutorImpl(SerialExecutorImpl const&) = delete;
  SerialExecutorImpl& operator=(SerialExecutorImpl const&) = delete;
  SerialExecutorImpl(SerialExecutorImpl&&) = delete;
  SerialExecutorImpl& operator=(SerialExecutorImpl&&) = delete;

  static KeepAlive<SerialExecutorImpl> create(
      KeepAlive<Executor> parent = getGlobalCPUExecutor());

  class Deleter {
   public:
    Deleter() {}

    void operator()(SerialExecutorImpl* executor) {
      executor->keepAliveRelease();
    }

   private:
    friend class SerialExecutorImpl;
    explicit Deleter(std::shared_ptr<Executor> parent)
        : parent_(std::move(parent)) {}

    std::shared_ptr<Executor> parent_;
  };

  using UniquePtr = std::unique_ptr<SerialExecutorImpl, Deleter>;
  [[deprecated("Replaced by create")]] static UniquePtr createUnique(
      std::shared_ptr<Executor> parent);

  /**
   * Add one task for execution in the parent executor
   */
  void add(Func func) override;

  /**
   * Add one task for execution in the parent executor, and use the given
   * priority for one task submission to parent executor.
   *
   * Since in-order execution of tasks submitted to SerialExecutor is
   * guaranteed, the priority given here does not necessarily reflect the
   * execution priority of the task submitted with this call to
   * `addWithPriority`. The given priority is passed on to the parent executor
   * for the execution of one of the SerialExecutor's tasks.
   */
  void addWithPriority(Func func, int8_t priority) override;
  uint8_t getNumPriorities() const override {
    return parent_->getNumPriorities();
  }

 private:
  struct Task {
    Func func;
    std::shared_ptr<RequestContext> ctx;
  };

  class Worker;

  explicit SerialExecutorImpl(KeepAlive<Executor> parent);
  ~SerialExecutorImpl() override;

  bool keepAliveAcquire() noexcept override;

  void keepAliveRelease() noexcept override;

  bool scheduleTask(Func&& func);
  void worker();
  void drain();

  KeepAlive<Executor> parent_;
  std::atomic<std::size_t> scheduled_{0};
  Queue<Task> queue_;

  std::atomic<ssize_t> keepAliveCounter_{1};
};

template <int LgQueueSegmentSize = 8>
struct SerialExecutorWithUnboundedQueue {
  // The consumer should only dequeue when the queue is non-empty, so we don't
  // need blocking.
  template <typename Task>
  using queue =
      folly::UMPSCQueue<Task, /* MayBlock */ false, LgQueueSegmentSize>;
  using type = SerialExecutorImpl<queue>;
};

template <class Task>
class SerialExecutorMPSCQueue;

} // namespace detail

using SerialExecutor =
    typename detail::SerialExecutorWithUnboundedQueue<>::type;

template <int LgQueueSegmentSize>
using SerialExecutorWithLgSegmentSize =
    typename detail::SerialExecutorWithUnboundedQueue<LgQueueSegmentSize>::type;

/**
 * SerialExecutor implementation that uses a mutex-protected queue. This uses
 * significantly less memory than SerialExecutor, at the expense of being more
 * susceptible to contention on add(). This is intended for use cases where
 * granular SerialExecutors are required, for example one per request. In these
 * scenarios, there are not many concurrent submitters, so contention is not an
 * issue, while memory overhead is.
 */
using SmallSerialExecutor =
    detail::SerialExecutorImpl<detail::SerialExecutorMPSCQueue>;

} // namespace folly

#include <folly/executors/SerialExecutor-inl.h>
