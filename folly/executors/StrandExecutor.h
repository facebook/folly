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

#include <folly/Optional.h>
#include <folly/concurrency/UnboundedQueue.h>
#include <folly/executors/SequencedExecutor.h>

namespace folly {
class StrandExecutor;

// StrandExecutor / StrandContext
//
// A StrandExecutor, like a SerialExecutor, serialises execution of work added
// to it, while deferring the actual execution of this work to another
// Executor.
//
// However, unlike the SerialExecutor, a StrandExecutor allows multiple
// StrandExecutors, each potentially delegating to a different parent Executor,
// to share the same serialising queue.
//
// This can be used in cases where you want to execute work on a
// caller-provided execution context, but want to make sure that work is
// serialised with respect to other work that might be scheduled on other
// underlying Executors.
//
// e.g. Using StrandExecutor with folly::coro::Task to enforce that only
//      a single thread accesses the object at a time, while still allowing
//      other other methods to execute while the current one is suspended
//
//    class MyObject {
//    public:
//
//      folly::coro::Task<T> someMethod() {
//        auto currentEx = co_await folly::coro::co_current_executor;
//        auto strandEx = folly::StrandExecutor::create(strand_, currentEx);
//        co_return co_await someMethodImpl().scheduleOn(strandEx);
//      }
//
//    private:
//      folly::coro::Task<T> someMethodImpl() {
//        // Safe to access otherState_ without further synchronisation.
//        modify(otherState_);
//
//        // Other instances of someMethod() calls will be able to run
//        // while this coroutine is suspended waiting for an RPC call
//        // to complete.
//        co_await getClient()->co_someRpcCall();
//
//        // When that call resumes, this coroutine is once again
//        // executing with mutual exclusion.
//        modify(otherState_);
//      }
//
//      std::shared_ptr<StrandContext> strand_{StrandContext::create()};
//      X otherState_;
//    };
//
class StrandContext : public std::enable_shared_from_this<StrandContext> {
 public:
  // Create a new StrandContext object. This will allow scheduling work
  // that will execute at most one task at a time but delegate the actual
  // execution to an execution context associated with each particular
  // function.
  static std::shared_ptr<StrandContext> create();

  // Schedule 'func()' to be called on 'executor' after all prior functions
  // scheduled to this context have completed.
  void add(Func func, Executor::KeepAlive<> executor);

  // Schedule `func()` to be called on `executor` with specified priority
  // after all prior functions scheduled to this context have completed.
  //
  // Note, that the priority will only affect the priority of the scheduling
  // of this particular function once all prior tasks have finished executing.
  void addWithPriority(
      Func func, Executor::KeepAlive<> executor, int8_t priority);

 private:
  struct PrivateTag {};
  class Task;

 public:
  // Public to allow construction using std::make_shared() but a logically
  // private constructor. Try to enforce this by forcing use of a private
  // tag-type as a parameter.
  explicit StrandContext(PrivateTag) {}

 private:
  struct QueueItem {
    Func func;
    Executor::KeepAlive<> executor;
    Optional<std::int8_t> priority;
  };

  void addImpl(QueueItem&& item);
  static void executeNext(std::shared_ptr<StrandContext> thisPtr) noexcept;
  static void dispatchFrontQueueItem(
      std::shared_ptr<StrandContext> thisPtr) noexcept;

  std::atomic<std::size_t> scheduled_{0};
  UMPSCQueue<QueueItem, /*MayBlock=*/false, /*LgSegmentSize=*/6> queue_;
};

class StrandExecutor final : public SequencedExecutor {
 public:
  // Creates a new StrandExecutor that is independent of other StrandExcutors.
  //
  // Work enqueued to the returned executor will be executed on the global CPU
  // thread-pool but will only execute at most one function enqueued to this
  // executor at a time.
  static Executor::KeepAlive<StrandExecutor> create();

  // Creates a new StrandExecutor that shares the serialised queue with other
  // StrandExecutors constructed with the same context.
  //
  // Work enqueued to the returned executor will be executed on the global CPU
  // thread-pool but will only execute at most one function enqueued to any
  // StrandExecutor created with the same StrandContext.
  static Executor::KeepAlive<StrandExecutor> create(
      std::shared_ptr<StrandContext> context);

  // Creates a new StrandExecutor that will serialise execution of any work
  // enqueued to it, running the work on the parentExecutor.
  static Executor::KeepAlive<StrandExecutor> create(
      Executor::KeepAlive<> parentExecutor);

  // Creates a new StrandExecutor that will execute work on 'parentExecutor'
  // but that will execute at most one function at a time enqueued to any
  // StrandExecutor created with the same StrandContext.
  static Executor::KeepAlive<StrandExecutor> create(
      std::shared_ptr<StrandContext> context,
      Executor::KeepAlive<> parentExecutor);

  void add(Func f) override;

  void addWithPriority(Func f, int8_t priority) override;
  uint8_t getNumPriorities() const override;

 protected:
  bool keepAliveAcquire() noexcept override;
  void keepAliveRelease() noexcept override;

 private:
  explicit StrandExecutor(
      std::shared_ptr<StrandContext> context,
      Executor::KeepAlive<> parent) noexcept;

 private:
  std::atomic<std::size_t> refCount_;
  Executor::KeepAlive<> parent_;
  std::shared_ptr<StrandContext> context_;
};

} // namespace folly
