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

#include <atomic>
#include <cassert>
#include <cstdint>

#include <folly/CPortability.h>
#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/experimental/coro/Coroutine.h>

namespace folly {

// Gets the instruction pointer of the return-address of the current function.
//
// Generally a function that uses this macro should be declared FOLLY_NOINLINE
// to prevent this returning surprising results in cases where the function
// is inlined.
#if FOLLY_HAS_BUILTIN(__builtin_return_address)
#define FOLLY_ASYNC_STACK_RETURN_ADDRESS() __builtin_return_address(0)
#else
#define FOLLY_ASYNC_STACK_RETURN_ADDRESS() static_cast<void*>(nullptr)
#endif

// Gets pointer to the current function invocation's stack-frame.
//
// Generally a function that uses this macro should be declared FOLLY_NOINLINE
// to prevent this returning surprising results in cases where the function
// is inlined.
#if FOLLY_HAS_BUILTIN(__builtin_frame_address)
#define FOLLY_ASYNC_STACK_FRAME_POINTER() __builtin_frame_address(0)
#else
#define FOLLY_ASYNC_STACK_FRAME_POINTER() static_cast<void*>(nullptr)
#endif

// This header defines data-structures used to represent an async stack-trace.
//
// These data-structures are intended for use by coroutines (and possibly other
// representations of async operations) to allow the current program to record
// an async-stack as a linked-list of async-stack-frames in a similar way to
// how a normal thread represents the stack as a linked-list of stack frames.
//
// From a high-level, just looking at the AsyncStackRoot/AsyncStackFrame
// data-structures, each thread maintains a linked-list of active AsyncStack
// chains that looks a bit like this.
//
//  Stack Register
//      |
//      V
//  Stack Frame   currentStackRoot (TLS)
//      |               |
//      V               V
//  Stack Frame <- AsyncStackRoot  -> AsyncStackFrame -> AsyncStackFrame -> ...
//      |               |
//      V               |
//  Stack Frame         |
//      :               |
//      V               V
//  Stack Frame <- AsyncStackRoot  -> AsyncStackFrame -> AsyncStackFrame -> ...
//      |               |
//      V               X
//  Stack Frame
//      :
//      V
//
// Whenever a thread enters an event loop or is about to execute some
// asynchronus callback/continuation the current thread registers an
// AsyncStackRoot and records the stack-frame of the normal thread
// stack that corresponds to that call so that each AsyncStackRoot
// can be later interleaved with a normal thread stack-trace at
// the appropriate location.
//
// Each AsyncStackRoot contains a pointer to the currently active
// AsyncStackFrame (if any). This AsyncStackFrame forms the head
// of a linked-list of AsyncStackFrame objects that represent the
// async stack-trace. Each non-head AsyncStackFrame is a suspended
// asynchronous operation, which is typically suspended waiting for
// the previous operation to complete.
//
//
// The following diagram shows in more detail how each of the fields
// in these data-structures relate to each other and how the
// async-stack interleaves with the normal thread-stack.
//
//      Current Thread Stack
//      ====================
// +------------------------------------+ <--- current top of stack
// | Normal Stack Frame                 |
// | - stack-base-pointer  ---.         |
// | - return-address         |         |          Thread Local Storage
// |                          |         |          ====================
// +--------------------------V---------+
// |         ...                        |     +-------------------------+
// |                          :         |     | - currentStackRoot  -.  |
// |                          :         |     |                      |  |
// +--------------------------V---------+     +----------------------|--+
// | Normal Stack Frame                 |                            |
// | - stack-base-pointer  ---.         |                            |
// | - return-address         |      .-------------------------------`
// |                          |      |  |
// +--------------------------V------|--+
// | Active Async Operation          |  |
// | (Callback or Coroutine)         |  |            Heap Allocated
// | - stack-base-pointer  ---.      |  |            ==============
// | - return-address         |      |  |
// | - pointer to async state | --------------> +-------------------------+
// |   (e.g. coro frame or    |      |  |       | Coroutine Frame         |
// |    future core)          |      |  |       | +---------------------+ |
// |                          |      |  |       | | Promise             | |
// +--------------------------V------|--+       | | +-----------------+ | |
// |   Event  / Callback             |  |   .------>| AsyncStackFrame | | |
// |   Loop     Callsite             |  |   |   | | | - parentFrame  --------.
// | - stack-base-pointer  ---.      |  |   |   | | | - instructionPtr| | |  |
// | - return-address         |      |  |   |   | | | - stackRoot -.  | | |  |
// |                          |      |  |   |   | | +--------------|--+ | |  |
// |  +--------------------+  |      |  |   |   | | ...            |    | |  |
// |  | AsyncStackRoot     |<--------`  |   |   | +----------------|----+ |  |
// |  | - topFrame   -----------------------`   | ...              |      |  |
// |  | - stackFramePtr -. |<---------------,   +------------------|------+  |
// |  | - nextRoot --.   | |  |         |   |                      |         |
// |  +--------------|---|-+  |         |   '----------------------`         |
// +-----------------|---V----V---------+       +-------------------------+  |
// |         ...     |                  |       | Coroutine Frame         |  |
// |                 |        :         |       |                         |  |
// |                 |        :         |       |  +-------------------+  |  |
// +-----------------|--------V---------+       |  | AsyncStackFrame   |<----`
// | Async Operation |                  |       |  | - parentFrame   --------.
// | (Callback/Coro) |                  |       |  | - instructionPtr  |  |  |
// |                 |        :         |       |  | - stackRoot       |  |  |
// |                 |        :         |       |  +-------------------+  |  |
// +-----------------|--------V---------+       +-------------------------+  |
// |  Event Loop /   |                  |                                    :
// |  Callback Call  |                  |                                    :
// | - frame-pointer | -------.         |                                    V
// | - return-address|        |         |
// |                 |        |         |      Another chain of potentially
// |  +--------------V-----+  |         |      unrelated AsyncStackFrame
// |  | AsyncStackRoot     |  |         |       +---------------------+
// |  | - topFrame  ---------------- - - - - >  | AsyncStackFrame     |
// |  | - stackFramePtr -. |  |         |       | - parentFrame -.    |
// |  | - nextRoot -.    | |  |         |       +----------------|----+
// |  +-------------|----|-+  |         |                        :
// |                |    |    |         |                        V
// +----------------|----V----V---------+
// |         ...    :                   |
// |                V                   |
// |                                    |
// +------------------------------------+
//
//
// This data-structure can be inspected from within the current process
// if desired, but is also intended to allow tools such as debuggers or
// profilers that are inspecting the memory of this process remotely.

struct AsyncStackRoot;
struct AsyncStackFrame;
namespace detail {
class ScopedAsyncStackRoot;
}

// Get access to the current thread's top-most AsyncStackRoot.
//
// Returns nullptr if there is no active AsyncStackRoot.
FOLLY_NODISCARD AsyncStackRoot* tryGetCurrentAsyncStackRoot() noexcept;

// Get access to the current thread's top-most AsyncStackRoot.
//
// Assumes that there is a current AsyncStackRoot.
FOLLY_NODISCARD AsyncStackRoot& getCurrentAsyncStackRoot() noexcept;

// Exchange the current thread's active AsyncStackRoot with the
// specified AsyncStackRoot pointer, returning the old AsyncStackRoot
// pointer.
//
// This is intended to be used to update the thread-local pointer
// when context-switching fiber stacks.
FOLLY_NODISCARD AsyncStackRoot* exchangeCurrentAsyncStackRoot(
    AsyncStackRoot* newRoot) noexcept;

// Perform some consistency checks on the specified AsyncStackFrame,
// assuming that it is the currently active AsyncStackFrame.
void checkAsyncStackFrameIsActive(const folly::AsyncStackFrame& frame) noexcept;

// Activate the specified AsyncStackFrame on the specified AsyncStackRoot,
// setting it as the current 'topFrame'.
//
// The AsyncStackRoot must be the current thread's top-most AsyncStackRoot
// and it must not currently have an active 'topFrame'.
//
// This is typically called immediately prior to executing a callback that
// resumes the async operation represented by 'frame'.
void activateAsyncStackFrame(
    folly::AsyncStackRoot& root, folly::AsyncStackFrame& frame) noexcept;

// Deactivate the specified AsyncStackFrame, clearing the current 'topFrame'.
//
// Typically called when the current async operation completes or is suspended
// and execution is about to return from the callback to the executor's event
// loop.
void deactivateAsyncStackFrame(folly::AsyncStackFrame& frame) noexcept;

// Push the 'callee' frame onto the current thread's async stack, deactivating
// the 'caller' frame and setting up the 'caller' to be the parent-frame of
// the 'callee'.
//
// The 'caller' frame must be the current thread's active frame.
//
// After this call, the 'callee' frame will be the current thread's active
// frame.
//
// This is typically used when one async operation is about to transfer
// execution to a child async operation. e.g. via a coroutine symmetric
// transfer.
void pushAsyncStackFrameCallerCallee(
    folly::AsyncStackFrame& callerFrame,
    folly::AsyncStackFrame& calleeFrame) noexcept;

// Pop the 'callee' frame off the stack, restoring the parent frame as the
// current frame.
//
// This is typically used when the current async operation completes and
// you are about to call/resume the caller. e.g. performing a symmetric
// transfer to the calling coroutine in final_suspend().
//
// If calleeFrame.getParentFrame() is null then this method is equivalent
// to deactivateAsyncStackFrame(), leaving no active AsyncStackFrame on
// the current AsyncStackRoot.
void popAsyncStackFrameCallee(folly::AsyncStackFrame& calleeFrame) noexcept;

// Get a pointer to a special frame that can be used as the root-frame
// for a chain of AsyncStackFrame that does not chain onto a normal
// call-stack.
//
// The caller should never modify this frame as it will be shared across
// many frames and threads. The implication of this restriction is that
// you should also never activate this frame.
AsyncStackFrame& getDetachedRootAsyncStackFrame() noexcept;

// Given an initial AsyncStackFrame, this will write `addresses` with
// the return addresses of the frames in this async stack trace, up to
// `maxAddresses` written.
// This assumes `addresses` has `maxAddresses` allocated space available.
// Returns the number of frames written.
size_t getAsyncStackTraceFromInitialFrame(
    folly::AsyncStackFrame* initialFrame,
    std::uintptr_t* addresses,
    size_t maxAddresses);

#if FOLLY_HAS_COROUTINES

// Resume the specified coroutine after installing a new AsyncStackRoot
// on the current thread and setting the specified AsyncStackFrame as
// the current async frame.
FOLLY_NOINLINE void resumeCoroutineWithNewAsyncStackRoot(
    coro::coroutine_handle<> h, AsyncStackFrame& frame) noexcept;

// Resume the specified coroutine after installing a new AsyncStackRoot
// on the current thread and setting the coroutine's associated
// AsyncStackFrame, obtained by calling promise.getAsyncFrame(), as the
// current async frame.
template <typename Promise>
void resumeCoroutineWithNewAsyncStackRoot(
    coro::coroutine_handle<Promise> h) noexcept;

#endif // FOLLY_HAS_COROUTINES

// An async stack frame contains information about a particular
// invocation of an asynchronous operation.
//
// For example, asynchronous operations implemented using coroutines
// would have each coroutine-frame contain an instance of AsyncStackFrame
// to record async-stack trace information for that coroutine invocation.
struct AsyncStackFrame {
 public:
  AsyncStackFrame() = default;

  // The parent frame is the frame of the async operation that is logically
  // the caller of this frame.
  AsyncStackFrame* getParentFrame() noexcept;
  const AsyncStackFrame* getParentFrame() const noexcept;
  void setParentFrame(AsyncStackFrame& frame) noexcept;

  // Get access to the current stack-root.
  //
  // This is only valid for either the root or leaf AsyncStackFrame
  // in a chain of frames.
  //
  // In the case of an active leaf-frame it is used as a cache to
  // avoid accessing the thread-local when pushing/popping frames.
  // In the case of the root frame (which has a null parent frame)
  // it points to an AsyncStackRoot that contains information about
  // the normal-stack caller.
  AsyncStackRoot* getStackRoot() noexcept;

  // The return address is generallty the address of the code in the
  // caller that will be executed when the operation owning the current
  // frame completes.
  void setReturnAddress(void* p = FOLLY_ASYNC_STACK_RETURN_ADDRESS()) noexcept;
  void* getReturnAddress() const noexcept;

 private:
  friend AsyncStackRoot;

  friend AsyncStackFrame& getDetachedRootAsyncStackFrame() noexcept;
  friend void activateAsyncStackFrame(
      folly::AsyncStackRoot&, folly::AsyncStackFrame&) noexcept;
  friend void deactivateAsyncStackFrame(folly::AsyncStackFrame&) noexcept;
  friend void pushAsyncStackFrameCallerCallee(
      folly::AsyncStackFrame&, folly::AsyncStackFrame&) noexcept;
  friend void checkAsyncStackFrameIsActive(
      const folly::AsyncStackFrame&) noexcept;
  friend void popAsyncStackFrameCallee(folly::AsyncStackFrame&) noexcept;

  // Pointer to the async caller's stack-frame info.
  //
  // This forms a linked-list of frames that make up a stack.
  // The list is terminated by a null pointer which indicates
  // the top of the async stack - either because the operation
  // is detached or because the next frame is a thread that is
  // blocked waiting for the async stack to complete.
  AsyncStackFrame* parentFrame = nullptr;

  // Instruction pointer of the caller of this frame.
  // This will typically be either the address of the continuation
  // of this asynchronous operation, or the address of the code
  // that launched this asynchronous operation. May be null
  // if the address is not known.
  //
  // Typically initialised with the result of a call to
  // FOLLY_ASYNC_STACK_RETURN_ADDRESS().
  void* instructionPointer = nullptr;

  // Pointer to the stack-root for the current thread.
  // Cache this in each async-stack frame so we don't have to
  // read from a thread-local to get the pointer.
  //
  // This pointer is only valid for the top-most stack frame.
  // When a frame is pushed or popped it should be copied to
  // the next frame, etc.
  //
  // The exception is for the bottom-most frame (ie. where
  // parentFrame == null). In this case, if stackRoot is non-null
  // then it points to a root that is currently blocked on some
  // thread waiting for the async work to complete. In this case
  // you can find the information about the stack-frame for that
  // thread in the AsyncStackRoot and can use it to continue
  // walking the stack-frames.
  AsyncStackRoot* stackRoot = nullptr;
};

// A stack-root represents the context of an event loop
// that is running some asynchronous work. The current async
// operation that is being executed by the event loop (if any)
// is pointed to by the 'topFrame'.
//
// If the current event loop is running nested inside some other
// event loop context then the 'nextRoot' points to the AsyncStackRoot
// context for the next event loop up the stack on the current thread.
//
// The 'stackFramePtr' holds a pointer to the normal stack-frame
// that is currently executing this event loop. This allows
// reconciliation of the parts between a normal stack-trace and
// the start of the async-stack trace.
//
// The current thread's top-most context (the head of the linked
// list of contexts) is obtained by calling getCurrentAsyncStackRoot().
struct AsyncStackRoot {
 public:
  // Sets the top-frame to be 'frame' and also updates the cached
  // 'frame.stackRoot' to be 'this'.
  //
  // The current stack root must not currently have any active
  // frame.
  void setTopFrame(AsyncStackFrame& frame) noexcept;
  AsyncStackFrame* getTopFrame() const noexcept;

  // Initialises this stack root with information about the context
  // in which the stack-root was declared. This records information
  // about where the async-stack-trace should be spliced into the
  // normal stack-trace.
  void setStackFrameContext(
      void* framePtr = FOLLY_ASYNC_STACK_FRAME_POINTER(),
      void* ip = FOLLY_ASYNC_STACK_RETURN_ADDRESS()) noexcept;
  void* getStackFramePointer() const noexcept;
  void* getReturnAddress() const noexcept;

  const AsyncStackRoot* getNextRoot() const noexcept;
  void setNextRoot(AsyncStackRoot* next) noexcept;

 private:
  friend class detail::ScopedAsyncStackRoot;
  friend void activateAsyncStackFrame(
      folly::AsyncStackRoot&, folly::AsyncStackFrame&) noexcept;
  friend void deactivateAsyncStackFrame(folly::AsyncStackFrame&) noexcept;
  friend void pushAsyncStackFrameCallerCallee(
      folly::AsyncStackFrame&, folly::AsyncStackFrame&) noexcept;
  friend void checkAsyncStackFrameIsActive(
      const folly::AsyncStackFrame&) noexcept;
  friend void popAsyncStackFrameCallee(folly::AsyncStackFrame&) noexcept;

  // Pointer to the currently-active AsyncStackFrame for this event
  // loop or callback invocation. May be null if this event loop is
  // not currently executing any async operations.
  //
  // This is atomic primarily to enforce visibility of writes to the
  // AsyncStackFrame that occur before the topFrame in other processes,
  // such as profilers/debuggers that may be running concurrently
  // with the current thread.
  std::atomic<AsyncStackFrame*> topFrame{nullptr};

  // Pointer to the next event loop context lower on the current
  // thread's stack.
  // This is nullptr if this is not a nested call to an event loop.
  AsyncStackRoot* nextRoot = nullptr;

  // Pointer to the stack-frame and return-address of the function
  // call that registered this AsyncStackRoot on the current thread.
  // This is generally the stack-frame responsible for executing async
  // callbacks (typically an event-loop).
  // Anything prior to this frame on the stack in the current thread
  // is potentially unrelated to the call-chain of the current async-stack.
  //
  // Typically initialised with FOLLY_ASYNC_STACK_FRAME_POINTER() or
  // setStackFrameContext().
  void* stackFramePtr = nullptr;

  // Typically initialise with FOLLY_ASYNC_STACK_RETURN_ADDRESS() or
  // setStackFrameContext().
  void* returnAddress = nullptr;
};

namespace detail {

class ScopedAsyncStackRoot {
 public:
  explicit ScopedAsyncStackRoot(
      void* framePointer = FOLLY_ASYNC_STACK_FRAME_POINTER(),
      void* returnAddress = FOLLY_ASYNC_STACK_RETURN_ADDRESS()) noexcept;
  ~ScopedAsyncStackRoot();

  void activateFrame(AsyncStackFrame& frame) noexcept {
    folly::activateAsyncStackFrame(root_, frame);
  }

 private:
  AsyncStackRoot root_;
};

} // namespace detail
} // namespace folly

#include <folly/tracing/AsyncStack-inl.h>
