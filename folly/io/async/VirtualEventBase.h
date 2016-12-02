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

#pragma once

#include <folly/Baton.h>
#include <folly/Executor.h>
#include <folly/io/async/EventBase.h>

namespace folly {

/**
 * VirtualEventBase implements a light-weight view onto existing EventBase.
 *
 * Multiple VirtualEventBases can be backed by a single EventBase. Similarly
 * to EventBase, VirtualEventBase implements loopKeepAlive() functionality,
 * which allows callbacks holding LoopKeepAlive token to keep EventBase looping
 * until they are complete.
 *
 * VirtualEventBase destructor blocks until all its KeepAliveTokens are released
 * and all tasks scheduled through it are complete. EventBase destructor also
 * blocks until all VirtualEventBases backed by it are released.
 */
class VirtualEventBase : public folly::Executor, public folly::TimeoutManager {
 public:
  explicit VirtualEventBase(EventBase& evb);

  VirtualEventBase(const VirtualEventBase&) = delete;
  VirtualEventBase& operator=(const VirtualEventBase&) = delete;

  ~VirtualEventBase();

  EventBase& getEventBase() {
    return evb_;
  }

  /**
   * Adds the given callback to a queue of things run before destruction
   * of current VirtualEventBase.
   *
   * This allows users of VirtualEventBase that run in it, but don't control it,
   * to be notified before VirtualEventBase gets destructed.
   *
   * Note: this will be called from the loop of the EventBase, backing this
   * VirtualEventBase
   */
  void runOnDestruction(EventBase::LoopCallback* callback);

  /**
   * @see EventBase::runInLoop
   */
  template <typename F>
  void runInLoop(F&& f, bool thisIteration = false) {
    evb_.runInLoop(std::forward<F>(f), thisIteration);
  }

  /**
   * VirtualEventBase destructor blocks until all tasks scheduled through its
   * runInEventBaseThread are complete.
   *
   * @see EventBase::runInEventBaseThread
   */
  template <typename F>
  void runInEventBaseThread(F&& f) {
    // LoopKeepAlive token has to be released in the EventBase thread. If
    // runInEventBaseThread() fails, we can't extract the LoopKeepAlive token
    // from the callback to properly release it.
    CHECK(evb_.runInEventBaseThread([
      keepAlive = loopKeepAliveAtomic(),
      f = std::forward<F>(f)
    ]() mutable { f(); }));
  }

  HHWheelTimer& timer() {
    return evb_.timer();
  }

  void attachTimeoutManager(
      AsyncTimeout* obj,
      TimeoutManager::InternalEnum internal) override {
    evb_.attachTimeoutManager(obj, internal);
  }

  void detachTimeoutManager(AsyncTimeout* obj) override {
    evb_.detachTimeoutManager(obj);
  }

  bool scheduleTimeout(AsyncTimeout* obj, TimeoutManager::timeout_type timeout)
      override {
    return evb_.scheduleTimeout(obj, timeout);
  }

  void cancelTimeout(AsyncTimeout* obj) override {
    evb_.cancelTimeout(obj);
  }

  void bumpHandlingTime() override {
    evb_.bumpHandlingTime();
  }

  bool isInTimeoutManagerThread() override {
    return evb_.isInTimeoutManagerThread();
  }

  /**
   * @see runInEventBaseThread
   */
  void add(folly::Func f) override {
    runInEventBaseThread(std::move(f));
  }

  struct LoopKeepAliveDeleter {
    void operator()(VirtualEventBase* evb) {
      DCHECK(evb->getEventBase().inRunningEventBaseThread());
      if (evb->loopKeepAliveCountAtomic_.load()) {
        evb->loopKeepAliveCount_ += evb->loopKeepAliveCountAtomic_.exchange(0);
      }
      DCHECK(evb->loopKeepAliveCount_ > 0);
      if (--evb->loopKeepAliveCount_ == 0) {
        evb->loopKeepAliveBaton_.post();
      }
    }
  };
  using LoopKeepAlive = std::unique_ptr<VirtualEventBase, LoopKeepAliveDeleter>;

  /**
   * Returns you a handle which prevents VirtualEventBase from being destroyed.
   * LoopKeepAlive handle can be released from EventBase loop only.
   *
   * loopKeepAlive() can be called from EventBase thread only.
   */
  LoopKeepAlive loopKeepAlive() {
    DCHECK(evb_.isInEventBaseThread());
    ++loopKeepAliveCount_;
    return LoopKeepAlive(this);
  }

  /**
   * Thread-safe version of loopKeepAlive()
   */
  LoopKeepAlive loopKeepAliveAtomic() {
    if (evb_.inRunningEventBaseThread()) {
      return loopKeepAlive();
    }
    ++loopKeepAliveCountAtomic_;
    return LoopKeepAlive(this);
  }

  bool inRunningEventBaseThread() const {
    return evb_.inRunningEventBaseThread();
  }

 private:
  using LoopCallbackList = EventBase::LoopCallback::List;

  EventBase& evb_;

  ssize_t loopKeepAliveCount_{0};
  std::atomic<ssize_t> loopKeepAliveCountAtomic_{0};
  folly::Baton<> loopKeepAliveBaton_;
  LoopKeepAlive loopKeepAlive_;

  EventBase::LoopKeepAlive evbLoopKeepAlive_;

  folly::Synchronized<LoopCallbackList> onDestructionCallbacks_;
};
}
