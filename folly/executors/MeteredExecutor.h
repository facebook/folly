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

#pragma once

#include <folly/DefaultKeepAliveExecutor.h>
#include <folly/io/async/AtomicNotificationQueue.h>

namespace folly {

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
class MeteredExecutor : public DefaultKeepAliveExecutor {
 public:
  using KeepAlive = Executor::KeepAlive<>;
  // owning constructor
  explicit MeteredExecutor(std::unique_ptr<Executor> exe);
  // non-owning constructor
  explicit MeteredExecutor(KeepAlive keepAlive);
  ~MeteredExecutor() override;

  void setMaxReadAtOnce(uint32_t maxAtOnce);

  void add(Func func) override;

  size_t pendingTasks() const { return queue_.size(); }

 private:
  void loopCallback();
  void scheduleCallback();

  class Consumer {
    Func first_;
    std::shared_ptr<RequestContext> firstRctx_;
    MeteredExecutor& self_;

   public:
    explicit Consumer(MeteredExecutor& self) : self_(self) {}
    void executeIfNotEmpty();
    void operator()(Func&& func, std::shared_ptr<RequestContext>&& rctx);
    ~Consumer();
  };
  folly::AtomicNotificationQueue<Func> queue_;
  std::unique_ptr<Executor> ownedExecutor_;
  KeepAlive kaInner_;
};

} // namespace folly
