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

#include <exception>

#include <folly/experimental/exception_tracer/ExceptionAbi.h>
#include <folly/experimental/exception_tracer/ExceptionTracer.h>
#include <folly/experimental/exception_tracer/ExceptionTracerLib.h>
#include <folly/experimental/exception_tracer/StackTrace.h>
#include <folly/experimental/symbolizer/Symbolizer.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

#if defined(__GLIBCXX__)

using namespace folly::exception_tracer;

namespace {

// If we somehow ended up in an invalid state, we don't want to print any stack
// trace at all because in could be bogus
thread_local bool invalid;

thread_local StackTraceStack uncaughtExceptions;
thread_local StackTraceStack caughtExceptions;

} // namespace

// These functions are exported and may be found via dlsym(RTLD_NEXT, ...)
extern "C" const StackTraceStack* getUncaughtExceptionStackTraceStack() {
  return invalid ? nullptr : &uncaughtExceptions;
}
extern "C" const StackTraceStack* getCaughtExceptionStackTraceStack() {
  return invalid ? nullptr : &caughtExceptions;
}

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
        [](void*, std::type_info*, void (**)(void*)) noexcept {
          addActiveException();
        });

    registerCxaBeginCatchCallback([](void*) noexcept {
      moveTopException(uncaughtExceptions, caughtExceptions);
    });

    registerCxaRethrowCallback([]() noexcept {
      moveTopException(caughtExceptions, uncaughtExceptions);
    });

    registerCxaEndCatchCallback([]() noexcept {
      if (invalid) {
        return;
      }

      __cxxabiv1::__cxa_exception* top =
          __cxxabiv1::__cxa_get_globals_fast()->caughtExceptions;
      // This is gcc specific and not specified in the ABI:
      // abs(handlerCount) is the number of active handlers, it's negative
      // for rethrown exceptions and positive (always 1) for regular
      // exceptions.
      // In the rethrow case, we've already popped the exception off the
      // caught stack, so we don't do anything here.
      // For Lua interop, we see the handlerCount = 0
      if ((top->handlerCount == 1) || (top->handlerCount == 0)) {
        if (!caughtExceptions.pop()) {
          uncaughtExceptions.clear();
          invalid = true;
        }
      }
    });

    registerRethrowExceptionCallback(
        [](std::exception_ptr) noexcept { addActiveException(); });

    try {
      ::folly::exception_tracer::installHandlers();
    } catch (...) {
    }
  }
};

Initializer initializer;

} // namespace

#endif // defined(__GLIBCXX__)

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
