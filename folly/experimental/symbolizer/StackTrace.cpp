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

#include <folly/experimental/symbolizer/StackTrace.h>

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

ssize_t getStackTrace(
    FOLLY_MAYBE_UNUSED uintptr_t* addresses,
    FOLLY_MAYBE_UNUSED size_t maxAddresses) {
  static_assert(
      sizeof(uintptr_t) == sizeof(void*), "uintptr_t / pointer size mismatch");
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

FOLLY_ALWAYS_INLINE
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

// Helper struct for manually walking the stack using stack frame pointers
struct StackFrame {
  StackFrame* parentFrame;
  void* returnAddress;
};

} // namespace

ssize_t getStackTraceSafe(
    FOLLY_MAYBE_UNUSED uintptr_t* addresses,
    FOLLY_MAYBE_UNUSED size_t maxAddresses) {
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
    FOLLY_MAYBE_UNUSED uintptr_t* addresses,
    FOLLY_MAYBE_UNUSED size_t maxAddresses) {
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

ssize_t getAsyncStackTraceSafe(uintptr_t* addresses, size_t maxAddresses) {
  size_t numFrames = 0;
  const auto* asyncStackRoot = tryGetCurrentAsyncStackRoot();
  // If we have no async stack root, this should return no frames.
  // If we do have a stack root, also include the current return address.
  if (asyncStackRoot != nullptr && numFrames < maxAddresses) {
    addresses[numFrames++] = (uintptr_t)FOLLY_ASYNC_STACK_RETURN_ADDRESS();
  }
  for (const auto* normalStackFrame =
           (StackFrame*)FOLLY_ASYNC_STACK_FRAME_POINTER();
       normalStackFrame != nullptr && asyncStackRoot != nullptr &&
       numFrames < maxAddresses;
       normalStackFrame = normalStackFrame->parentFrame) {
    // Walk the normal stack to find the caller of the frame that holds the
    // AsyncStackRoot. If the caller holds the AsyncStackRoot, then the
    // current frame is part of an async operation, and we should get the
    // async stack trace before adding the current frame.
    if (normalStackFrame->parentFrame ==
        asyncStackRoot->getStackFramePointer()) {
      // Follow the async stack trace starting from the root
      numFrames += getAsyncStackTraceFromInitialFrame(
          asyncStackRoot->getTopFrame(),
          &addresses[numFrames],
          maxAddresses - numFrames);
      // There could be more related work at the next async stack root.
      // Anything after the stack frame containing the last async stack root
      // is potentially unrelated to the current async stack.
      asyncStackRoot = asyncStackRoot->getNextRoot();
      if (asyncStackRoot == nullptr) {
        break;
      }
    }
    addresses[numFrames++] = (uintptr_t)normalStackFrame->returnAddress;
  }
  return numFrames;
}

} // namespace symbolizer
} // namespace folly
