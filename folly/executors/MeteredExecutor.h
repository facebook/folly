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

#include <optional>

#include <folly/DefaultKeepAliveExecutor.h>
#include <folly/executors/QueueObserver.h>
#include <folly/io/async/AtomicNotificationQueue.h>

namespace folly {

namespace detail {
// Attaches a queue to an already existing executor and exposes Executor
// interface to add tasks to that queue.
// Consumption from this queue is "metered". Specifically, only a limited number
// of tasks scheduled onto the resulting executor will be ever simultaneously
// present in the wrapped executor's queue. This mechanism can be used e.g. to
// attach lower priority queue to an already existing executor -
// meaning tasks scheduled on the wrapper will, in practice, yield to task
// scheduled directly on the wrapped executor.
//
// Notice that multi level priorities can be easily achieved via chaining,
// for example:
// auto hipri_exec = std::make_unique<CPUThreadPoolExecutor>(numThreads);
// auto hipri_ka = getKeepAliveToken(hipri_exec.get());
// auto mipri_exec = std::make_unique<MeteredExecutor>(hipri_ka);
// auto mipri_ka = getKeepAliveToken(mipri_exec.get());
// auto lopri_exec = std::make_unique<MeteredExecutor>(mipri_ka);
// auto lopri_ka = getKeepAliveToken(lopri_exec.get());
template <template <typename> class Atom = std::atomic>
class MeteredExecutorImpl : public DefaultKeepAliveExecutor {
 public:
  struct Options {
    Options() {}
    std::string name;
    bool enableQueueObserver{false};
    size_t numPriorities{1};
    int8_t priority{0};
  };

  using KeepAlive = Executor::KeepAlive<>;
  // owning constructor
  explicit MeteredExecutorImpl(
      std::unique_ptr<Executor> exe, Options options = Options());
  // non-owning constructor
  explicit MeteredExecutorImpl(
      KeepAlive keepAlive, Options options = Options());
  ~MeteredExecutorImpl() override;

  void add(Func func) override;

  size_t pendingTasks() const { return queue_.size(); }

  // Pause executing tasks. Subsequent resume() necessary to
  // start executing tasks again. Can be invoked from any thread and only
  // prevents scheduling of future tasks for executions i.e. doesn't impact
  // tasks already scheduled. The method returns `false` if we invoke `pause`
  // on an already paused executor.
  bool pause();

  // Resume executing tasks. no-op if not paused. Can be invoked from any
  // thread. Returns `false` if `resume` is invoked on an executor that's
  // not paused.
  bool resume();

 private:
  class Task {
   public:
    Task() = default;

    explicit Task(Func&& func) : func_(std::move(func)) {}

    void setQueueObserverPayload(intptr_t newValue) {
      queueObserverPayload_ = newValue;
    }

    intptr_t getQueueObserverPayload() const { return queueObserverPayload_; }

    void run() { func_(); }

    bool hasFunc() { return func_ ? true : false; }

   private:
    Func func_;
    intptr_t queueObserverPayload_;
  };

  std::unique_ptr<folly::QueueObserver> setupQueueObserver();
  void loopCallback();
  void scheduleCallback();

  // Atomically checks if we should scheduled loopCallback and records that
  // such a callback was skipped so that a subsequent resume can schedule one.
  bool shouldSkip();

  class Consumer {
    std::optional<Task> first_;
    std::shared_ptr<RequestContext> firstRctx_;
    MeteredExecutorImpl& self_;

   public:
    explicit Consumer(MeteredExecutorImpl& self) : self_(self) {}
    void executeIfNotEmpty();
    void operator()(Task&& task, std::shared_ptr<RequestContext>&& rctx);
    ~Consumer();
  };
  Options options_;
  folly::AtomicNotificationQueue<Task> queue_;
  std::unique_ptr<Executor> ownedExecutor_;
  KeepAlive kaInner_;
  std::unique_ptr<folly::QueueObserver> queueObserver_{setupQueueObserver()};

  // RUNNING - default state where tasks can be enqueue and will be executed on
  //           wrapped executor
  // PAUSED - executor is paused so no tasks will be scheduled for executions
  //          add() can still happen
  // PAUSED_PENDING - Same as paused but records that we had skipped a
  //                  loopCallback while paused
  //
  //                       resume()
  //         |-------<--------------------------------------|
  //         v                                              ^
  //         |                                              |
  //  +---------+ pause() +---------+ loopCallback() +--------------+
  //  | Running |---->----| Paused  |------->--------|PausedPending |
  //  +---------+         +---------+                +--------------+
  //       |                   |
  //       ^                   V
  //       |---------<---------|
  //             resume()
  //
  enum ExecutorState : uint8_t { RUNNING = 0, PAUSED = 1, PAUSED_PENDING = 2 };
  struct State {
    Atom<uint8_t> execState{RUNNING};
    // We expect only one instance of loopCallback to be scheduled.
    // callbackScheduled helps assert this invariant.
    Atom<bool> callbackScheduled{false};
  };
  State state_;
};

} // namespace detail

using MeteredExecutor = detail::MeteredExecutorImpl<>;

} // namespace folly

#include <folly/executors/MeteredExecutor-inl.h>
