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

#include <exception>
#include <type_traits>

#include <folly/Utility.h>
#include <folly/debugging/exception_tracer/ExceptionAbi.h>
#include <folly/debugging/exception_tracer/ExceptionTracer.h>
#include <folly/debugging/exception_tracer/ExceptionTracerLib.h>
#include <folly/debugging/exception_tracer/StackTrace.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

#if FOLLY_HAS_EXCEPTION_TRACER

using namespace folly::exception_tracer;

namespace {

// If we somehow ended up in an invalid state, we don't want to print any stack
// trace at all because in could be bogus
thread_local bool invalid;

thread_local StackTraceStack uncaughtExceptions;
thread_local StackTraceStack caughtExceptions;

// StackTraceStack should be usable as thread_local for the entire lifetime of
// a thread, and not just up until thread_local variables are destroyed
static_assert(std::is_trivially_destructible_v<StackTraceStack>);
static_assert(folly::is_constexpr_default_constructible_v<StackTraceStack>);

} // namespace

// These functions are exported and may be found via dlsym(RTLD_NEXT, ...)
extern "C" const StackTraceStack*
folly_exception_tracer_get_uncaught_exceptions_stack_trace_stack() {
  return invalid ? nullptr : &uncaughtExceptions;
}
extern "C" const StackTraceStack*
folly_exception_tracer_get_caught_exceptions_stack_trace_stack() {
  return invalid ? nullptr : &caughtExceptions;
}

namespace folly {

namespace detail {
/// access_cxxabi
///
/// When building folly with thread sanitizer but using a prebuilt libstdc++ or
/// libc++, folly's accesses here are instrumented while the standard library's
/// accesses are not. This can cause false positive data-race reports. This
/// namespace is here as an easy way to set up thread-sanitizer suppressions.
///
/// One scenario is when the __cxa_end_catch folly hook does an access, then the
/// last std::exception_ptr to the same exception is released. The exception_ptr
/// tor does an uninstrumented access and then calls free, which like malloc is
/// instrumented. The thread-sanitizer runtime observes only an access in folly
/// in one thread followed by a call to free in another thread, but does not
/// observe the synchronizing refcount-decrement in between.
///
/// Aside from suppressions, there are two other solutions:
/// * Never access cxxabi data directly.
/// * Use an instrumented build of the standard library when building folly or
///   the application with instrumentation.
namespace access_cxxabi {

static auto get_caught_exceptions_handler_count() {
  return __cxxabiv1::__cxa_get_globals_fast()->caughtExceptions->handlerCount;
}

} // namespace access_cxxabi

} // namespace detail

namespace {

void addActiveException() {
  // Capture stack trace
  if (!invalid) {
    if (!uncaughtExceptions.pushCurrent()) {
      uncaughtExceptions.clear();
      caughtExceptions.clear();
      invalid = true;
    }
  }
}

void moveTopException(StackTraceStack& from, StackTraceStack& to) {
  if (invalid) {
    return;
  }
  if (!to.moveTopFrom(from)) {
    from.clear();
    to.clear();
    invalid = true;
  }
}

struct Initializer {
  Initializer() {
    registerCxaThrowCallback(
        *+[](void*, std::type_info*, void (**)(void*)) noexcept {
          addActiveException();
        });

    registerCxaBeginCatchCallback(*+[](void*) noexcept {
      moveTopException(uncaughtExceptions, caughtExceptions);
    });

    registerCxaRethrowCallback(*+[]() noexcept {
      moveTopException(caughtExceptions, uncaughtExceptions);
    });

    registerCxaEndCatchCallback(*+[]() noexcept {
      if (invalid) {
        return;
      }

      auto topHandlerCount =
          detail::access_cxxabi::get_caught_exceptions_handler_count();
      // This is gcc specific and not specified in the ABI:
      // abs(handlerCount) is the number of active handlers, it's negative
      // for rethrown exceptions and positive (always 1) for regular
      // exceptions.
      // In the rethrow case, we've already popped the exception off the
      // caught stack, so we don't do anything here.
      // For Lua interop, we see the handlerCount = 0
      if ((topHandlerCount == 1) || (topHandlerCount == 0)) {
        if (!caughtExceptions.pop()) {
          uncaughtExceptions.clear();
          invalid = true;
        }
      }
    });

    registerRethrowExceptionCallback(*+[](std::exception_ptr) noexcept {
      addActiveException();
    });

    try {
      ::folly::exception_tracer::installHandlers();
    } catch (...) {
    }
  }
};

Initializer initializer;

} // namespace

} // namespace folly

#endif //  FOLLY_HAS_EXCEPTION_TRACER

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
