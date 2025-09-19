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
 *
 * NOTE: This describes low-level executor tasks. Not coro::Task tasks.
 * This executor does not guarantee that coro::Task tasks will be completed in
 * the order that they are added. Rather, a coro::Task task may be suspended at
 * a co_await/co_yield point and another such task that has been added to this
 * executor may be resumed at that point.
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

  const KeepAlive<Executor>& parent() const { return parent_; }

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
  std::atomic<ssize_t> keepAliveCounter_{1};
  Queue<Task> queue_;
};

class NoopMutex;

template <class Task, class Mutex = folly::DistributedMutex>
class SerialExecutorMPSCQueue;

template <typename Task>
using SerialExecutorQueue = SerialExecutorMPSCQueue<Task>;

template <typename Task>
using SPSerialExecutorQueue = SerialExecutorMPSCQueue<Task, NoopMutex>;

} // namespace detail

using SerialExecutor = detail::SerialExecutorImpl<detail::SerialExecutorQueue>;

/**
 * Single-producer version of SmallExecutor. It is the responsibility of the
 * caller to guarantee that calls to add() are externally serialized, but it can
 * be slightly faster.
 *
 * It is very unlikely that you need this.
 */
using SPSerialExecutor =
    detail::SerialExecutorImpl<detail::SPSerialExecutorQueue>;

} // namespace folly

#include <folly/executors/SerialExecutor-inl.h>
