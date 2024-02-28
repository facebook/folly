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

#include <folly/experimental/symbolizer/StackTrace.h>
#include <folly/tracing/AsyncStack.h>

#include <memory>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/portability/Config.h>
#include <folly/tracing/AsyncStack.h>

#if FOLLY_HAVE_LIBUNWIND
// Must be first to ensure that UNW_LOCAL_ONLY is defined
#define UNW_LOCAL_ONLY 1
#include <folly/portability/Libunwind.h>
#endif

#if FOLLY_HAVE_BACKTRACE
#include <execinfo.h>
#endif

namespace folly {
namespace symbolizer {

namespace {
// force a getStackTrace to work around a race condition in the
// libunwind tdep_init
static uintptr_t sAddr = 0;
static ssize_t sInit = getStackTrace(&sAddr, 0);
} // namespace

ssize_t getStackTrace(
    [[maybe_unused]] uintptr_t* addresses,
    [[maybe_unused]] size_t maxAddresses) {
  static_assert(
      sizeof(uintptr_t) == sizeof(void*), "uintptr_t / pointer size mismatch");
  std::ignore = sInit;
  // The libunwind documentation says that unw_backtrace is
  // async-signal-safe but, as of libunwind 1.0.1, it isn't
  // (tdep_trace allocates memory on x86_64)
  //
  // There are two major variants of libunwind. libunwind on Linux
  // (https://www.nongnu.org/libunwind/) provides unw_backtrace, and
  // Apache/LLVM libunwind (notably used on Apple platforms)
  // doesn't. They can be distinguished with the UNW_VERSION #define.
  //
  // When unw_backtrace is not available, fall back on the standard
  // `backtrace` function from execinfo.h.
#if FOLLY_HAVE_LIBUNWIND && defined(UNW_VERSION)
  int r = unw_backtrace(reinterpret_cast<void**>(addresses), maxAddresses);
  return r < 0 ? -1 : r;
#elif FOLLY_HAVE_BACKTRACE
  int r = backtrace(reinterpret_cast<void**>(addresses), maxAddresses);
  return r < 0 ? -1 : r;
#elif FOLLY_HAVE_LIBUNWIND
  return getStackTraceSafe(addresses, maxAddresses);
#else
  return -1;
#endif
}

namespace {

// Heuristic for guessing the maximum stack frame size. This is needed to ensure
// we do not have stack corruption while walking the stack.
constexpr size_t kMaxExpectedStackFrameSizeLg2 = sizeof(size_t) == 8
    ? 36 // 64GB
    : 28 // 256MB
    ;
constexpr size_t kMaxExpectedStackFrameSize //
    = size_t(1) << kMaxExpectedStackFrameSizeLg2;

#if FOLLY_HAVE_LIBUNWIND

inline bool getFrameInfo(unw_cursor_t* cursor, uintptr_t& ip) {
  unw_word_t uip;
  if (unw_get_reg(cursor, UNW_REG_IP, &uip) < 0) {
    return false;
  }
  int r = unw_is_signal_frame(cursor);
  if (r < 0) {
    return false;
  }
  // Use previous instruction in normal (call) frames (because the
  // return address might not be in the same function for noreturn functions)
  // but not in signal frames.
  ip = uip - (r == 0);
  return true;
}

// on ppc64le, fails with
// function can never be inlined because it uses setjmp
#if FOLLY_PPC64 == 0
FOLLY_ALWAYS_INLINE
#endif
ssize_t getStackTraceInPlace(
    unw_context_t& context,
    unw_cursor_t& cursor,
    uintptr_t* addresses,
    size_t maxAddresses) {
  if (maxAddresses == 0) {
    return 0;
  }
  if (unw_getcontext(&context) < 0) {
    return -1;
  }
  if (unw_init_local(&cursor, &context) < 0) {
    return -1;
  }
  if (!getFrameInfo(&cursor, *addresses)) {
    return -1;
  }
  ++addresses;
  size_t count = 1;
  for (; count != maxAddresses; ++count, ++addresses) {
    int r = unw_step(&cursor);
    if (r < 0) {
      return -1;
    }
    if (r == 0) {
      break;
    }
    if (!getFrameInfo(&cursor, *addresses)) {
      return -1;
    }
  }
  return count;
}

#endif // FOLLY_HAVE_LIBUNWIND
} // namespace

ssize_t getStackTraceSafe(
    [[maybe_unused]] uintptr_t* addresses,
    [[maybe_unused]] size_t maxAddresses) {
  std::ignore = sInit;
#if defined(__APPLE__)
  // While Apple platforms support libunwind, the unw_init_local,
  // unw_step step loop does not cross the boundary from async signal
  // handlers to the aborting code, while `backtrace` from execinfo.h
  // does. `backtrace` is not explicitly documented on either macOS or
  // Linux to be async-signal-safe, but the implementation in
  // https://opensource.apple.com/source/Libc/Libc-1353.60.8/, and it is
  // widely used in signal handlers in practice.
  return backtrace(reinterpret_cast<void**>(addresses), maxAddresses);
#elif FOLLY_HAVE_LIBUNWIND
  unw_context_t context;
  unw_cursor_t cursor;
  return getStackTraceInPlace(context, cursor, addresses, maxAddresses);
#else
  return -1;
#endif
}

ssize_t getStackTraceHeap(
    [[maybe_unused]] uintptr_t* addresses,
    [[maybe_unused]] size_t maxAddresses) {
  std::ignore = sInit;
#if FOLLY_HAVE_LIBUNWIND
  struct Ctx {
    unw_context_t context;
    unw_cursor_t cursor;
  };
  auto ctx_ptr = std::make_unique<Ctx>();
  if (!ctx_ptr) {
    return -1;
  }
  return getStackTraceInPlace(
      ctx_ptr->context, ctx_ptr->cursor, addresses, maxAddresses);
#else
  return -1;
#endif
}

namespace {
// Helper struct for manually walking the stack using stack frame pointers
struct StackFrame {
  StackFrame* parentFrame;
  void* returnAddress;
};

FOLLY_DISABLE_THREAD_SANITIZER size_t walkNormalStack(
    uintptr_t* addresses,
    size_t maxAddresses,
    StackFrame* normalStackFrame,
    StackFrame* normalStackFrameStop) {
  size_t numFrames = 0;
  while (numFrames < maxAddresses && normalStackFrame != nullptr) {
    auto* normalStackFrameNext = normalStackFrame->parentFrame;
    if (!(normalStackFrameNext > normalStackFrame &&
          normalStackFrameNext <
              normalStackFrame + kMaxExpectedStackFrameSize)) {
      // Stack frame addresses should increase as we traverse the stack.
      // If it doesn't, it means we have stack corruption, or an unusual calling
      // convention. Ensure that each subsequent frame's address is within a
      // valid range. If it does not, stop walking the stack early to avoid
      // incorrect stack walking.
      break;
    }
    if (normalStackFrameStop != nullptr &&
        normalStackFrameNext == normalStackFrameStop) {
      // Reached end of normal stack, need to transition to the async stack.
      // Do not include the return address in the stack trace that points
      // to the frame that registered the AsyncStackRoot.
      // Use the return address from the AsyncStackFrame as the current frame's
      // return address rather than the return address from the normal
      // stack frame, which would be the address of the executor function
      // that invoked the callback.
      break;
    }
    addresses[numFrames++] =
        reinterpret_cast<std::uintptr_t>(normalStackFrame->returnAddress);
    normalStackFrame = normalStackFrameNext;
  }
  return numFrames;
}

struct WalkAsyncStackResult {
  // Number of frames added in this walk
  size_t numFrames{0};
  // Normal stack frame to start the next normal stack walk
  StackFrame* normalStackFrame{nullptr};
  StackFrame* normalStackFrameStop{nullptr};
  // Async stack frame to start the next async stack walk after the next
  // normal stack walk
  AsyncStackFrame* asyncStackFrame{nullptr};
};

WalkAsyncStackResult walkAsyncStack(
    uintptr_t* addresses,
    size_t maxAddresses,
    AsyncStackFrame* asyncStackFrame) {
  WalkAsyncStackResult result;
  while (result.numFrames < maxAddresses && asyncStackFrame != nullptr) {
    addresses[result.numFrames++] =
        reinterpret_cast<std::uintptr_t>(asyncStackFrame->getReturnAddress());

    auto* asyncStackFrameNext = asyncStackFrame->getParentFrame();
    if (asyncStackFrameNext == nullptr) {
      // Reached end of async-stack.
      // Check if there is an AsyncStackRoot and if so, whether there
      // is an associated stack frame that indicates the normal stack
      // frame we should continue walking at.
      const auto* asyncStackRoot = asyncStackFrame->getStackRoot();
      if (asyncStackRoot == nullptr) {
        // This is a detached async stack. We are done
        break;
      }

      // Get the normal stack frame holding this async root.
      result.normalStackFrame =
          reinterpret_cast<StackFrame*>(asyncStackRoot->getStackFramePointer());
      if (result.normalStackFrame == nullptr) {
        // No associated normal stack frame for this async stack root.
        // This means we should treat this as a top-level/detached
        // stack and not try to walk any further.
        break;
      }
      // Skip to the parent stack-frame pointer
      result.normalStackFrame = result.normalStackFrame->parentFrame;

      // Check if there is a higher-level AsyncStackRoot that defines
      // the stop point we should stop walking normal stack frames at.
      // If there is no higher stack root then we will walk to the
      // top of the normal stack (normalStackFrameStop == nullptr).
      // Otherwise we record the frame pointer that we should stop
      // at and walk normal stack frames until we hit that frame.
      // Also get the async stack frame where the next async stack walk
      // should begin after the next normal stack walk finishes.
      asyncStackRoot = asyncStackRoot->getNextRoot();
      if (asyncStackRoot != nullptr) {
        result.normalStackFrameStop = reinterpret_cast<StackFrame*>(
            asyncStackRoot->getStackFramePointer());
        result.asyncStackFrame = asyncStackRoot->getTopFrame();
      }
    }
    asyncStackFrame = asyncStackFrameNext;
  }
  return result;
}
} // namespace

ssize_t getAsyncStackTraceSafe(uintptr_t* addresses, size_t maxAddresses) {
  size_t numFrames = 0;
  const auto* asyncStackRoot = tryGetCurrentAsyncStackRoot();
  if (asyncStackRoot == nullptr) {
    // No async operation in progress. Return empty stack
    return numFrames;
  }

  // Start by walking the normal stack until we get to the frame right before
  // the frame that holds the async root.
  auto* normalStackFrame =
      reinterpret_cast<StackFrame*>(FOLLY_ASYNC_STACK_FRAME_POINTER());
  auto* normalStackFrameStop =
      reinterpret_cast<StackFrame*>(asyncStackRoot->getStackFramePointer());
  if (numFrames < maxAddresses) {
    addresses[numFrames++] =
        reinterpret_cast<std::uintptr_t>(FOLLY_ASYNC_STACK_RETURN_ADDRESS());
  }
  auto* asyncStackFrame = asyncStackRoot->getTopFrame();

  while (numFrames < maxAddresses &&
         (normalStackFrame != nullptr || asyncStackFrame != nullptr)) {
    numFrames += walkNormalStack(
        addresses + numFrames,
        maxAddresses - numFrames,
        normalStackFrame,
        normalStackFrameStop);

    auto walkAsyncStackResult = walkAsyncStack(
        addresses + numFrames, maxAddresses - numFrames, asyncStackFrame);
    numFrames += walkAsyncStackResult.numFrames;
    normalStackFrame = walkAsyncStackResult.normalStackFrame;
    normalStackFrameStop = walkAsyncStackResult.normalStackFrameStop;
    asyncStackFrame = walkAsyncStackResult.asyncStackFrame;
  }
  return numFrames;
}

} // namespace symbolizer
} // namespace folly
