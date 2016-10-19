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

#include <functional>
#include <thread>
#include <typeinfo>

#include <folly/AtomicIntrusiveLinkedList.h>
#include <folly/CPortability.h>
#include <folly/Function.h>
#include <folly/IntrusiveList.h>
#include <folly/Portability.h>
#include <folly/fibers/BoostContextCompatibility.h>
#include <folly/io/async/Request.h>

namespace folly {
namespace fibers {

class Baton;
class FiberManager;

/**
 * @class Fiber
 * @brief Fiber object used by FiberManager to execute tasks.
 *
 * Each Fiber object can be executing at most one task at a time. In active
 * phase it is running the task function and keeps its context.
 * Fiber is also used to pass data to blocked task and thus unblock it.
 * Each Fiber may be associated with a single FiberManager.
 */
class Fiber {
 public:
  /**
   * Sets data for the blocked task
   *
   * @param data this data will be returned by await() when task is resumed.
   */
  void setData(intptr_t data);

  Fiber(const Fiber&) = delete;
  Fiber& operator=(const Fiber&) = delete;

  ~Fiber();

  /**
   * Retrieve this fiber's base stack and stack size.
   *
   * @return This fiber's stack pointer and stack size.
   */
  std::pair<void*, size_t> getStack() const {
    void* const stack =
        std::min<void*>(fcontext_.stackLimit(), fcontext_.stackBase());
    const size_t size = std::abs(
        reinterpret_cast<intptr_t>(fcontext_.stackBase()) -
        reinterpret_cast<intptr_t>(fcontext_.stackLimit()));
    return {stack, size};
  }

 private:
  enum State {
    INVALID, /**< Does't have task function */
    NOT_STARTED, /**< Has task function, not started */
    READY_TO_RUN, /**< Was started, blocked, then unblocked */
    RUNNING, /**< Is running right now */
    AWAITING, /**< Is currently blocked */
    AWAITING_IMMEDIATE, /**< Was preempted to run an immediate function,
                             and will be resumed right away */
    YIELDED, /**< The fiber yielded execution voluntarily */
  };

  State state_{INVALID}; /**< current Fiber state */

  friend class Baton;
  friend class FiberManager;

  explicit Fiber(FiberManager& fiberManager);

  void init(bool recordStackUsed);

  template <typename F>
  void setFunction(F&& func);

  template <typename F, typename G>
  void setFunctionFinally(F&& func, G&& finally);

  static void fiberFuncHelper(intptr_t fiber);
  void fiberFunc();

  /**
   * Switch out of fiber context into the main context,
   * performing necessary housekeeping for the new state.
   *
   * @param state New state, must not be RUNNING.
   *
   * @return The value passed back from the main context.
   */
  intptr_t preempt(State state);

  /**
   * Examines how much of the stack we used at this moment and
   * registers with the FiberManager (for monitoring).
   */
  void recordStackPosition();

  FiberManager& fiberManager_; /**< Associated FiberManager */
  FContext fcontext_; /**< current task execution context */
  intptr_t data_; /**< Used to keep some data with the Fiber */
  std::shared_ptr<RequestContext> rcontext_; /**< current RequestContext */
  folly::Function<void()> func_; /**< task function */
  bool recordStackUsed_{false};
  bool stackFilledWithMagic_{false};

  /**
   * Points to next fiber in remote ready list
   */
  folly::AtomicIntrusiveLinkedListHook<Fiber> nextRemoteReady_;

  static constexpr size_t kUserBufferSize = 256;
  std::aligned_storage<kUserBufferSize>::type userBuffer_;

  void* getUserBuffer();

  folly::Function<void()> resultFunc_;
  folly::Function<void()> finallyFunc_;

  class LocalData {
   public:
    LocalData() {}
    LocalData(const LocalData& other);
    LocalData& operator=(const LocalData& other);

    template <typename T>
    T& get() {
      if (data_) {
        assert(*dataType_ == typeid(T));
        return *reinterpret_cast<T*>(data_);
      }
      return getSlow<T>();
    }

    void reset();

    // private:
    template <typename T>
    FOLLY_NOINLINE T& getSlow();

    static void* allocateHeapBuffer(size_t size);
    static void freeHeapBuffer(void* buffer);

    template <typename T>
    static void dataCopyConstructor(void*, const void*);
    template <typename T>
    static void dataBufferDestructor(void*);
    template <typename T>
    static void dataHeapDestructor(void*);

    static constexpr size_t kBufferSize = 128;
    std::aligned_storage<kBufferSize>::type buffer_;
    size_t dataSize_;

    const std::type_info* dataType_;
    void (*dataDestructor_)(void*);
    void (*dataCopyConstructor_)(void*, const void*);
    void* data_{nullptr};
  };

  LocalData localData_;

  folly::IntrusiveListHook listHook_; /**< list hook for different FiberManager
                                           queues */
  folly::IntrusiveListHook globalListHook_; /**< list hook for global list */
  std::thread::id threadId_{};

#ifdef FOLLY_SANITIZE_ADDRESS
  void* asanFakeStack_{nullptr};
  const void* asanMainStackBase_{nullptr};
  size_t asanMainStackSize_{0};
#endif
};
}
}

#include <folly/fibers/Fiber-inl.h>
