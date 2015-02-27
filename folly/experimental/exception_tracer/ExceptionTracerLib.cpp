/*
 * Copyright 2015 Facebook, Inc.
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

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

#include <glog/logging.h>

#include <folly/Portability.h>
#include <folly/experimental/exception_tracer/StackTrace.h>
#include <folly/experimental/exception_tracer/ExceptionAbi.h>
#include <folly/experimental/exception_tracer/ExceptionTracer.h>
#include <folly/experimental/symbolizer/Symbolizer.h>

namespace __cxxabiv1 {

extern "C" {
FOLLY_NORETURN void __cxa_throw(void* thrownException,
                                std::type_info* type,
                                void (*destructor)(void*));
void* __cxa_begin_catch(void* excObj);
FOLLY_NORETURN void __cxa_rethrow(void);
void __cxa_end_catch(void);
}

}  // namespace __cxxabiv1

using namespace folly::exception_tracer;

namespace {

FOLLY_TLS bool invalid;
FOLLY_TLS StackTraceStack activeExceptions;
FOLLY_TLS StackTraceStack caughtExceptions;
pthread_once_t initialized = PTHREAD_ONCE_INIT;

extern "C" {
FOLLY_NORETURN typedef void (*CxaThrowType)(void*,
                                            std::type_info*,
                                            void (*)(void*));
typedef void* (*CxaBeginCatchType)(void*);
FOLLY_NORETURN typedef void (*CxaRethrowType)(void);
typedef void (*CxaEndCatchType)(void);

CxaThrowType orig_cxa_throw;
CxaBeginCatchType orig_cxa_begin_catch;
CxaRethrowType orig_cxa_rethrow;
CxaEndCatchType orig_cxa_end_catch;
}  // extern "C"

FOLLY_NORETURN typedef void (*RethrowExceptionType)(std::exception_ptr);
RethrowExceptionType orig_rethrow_exception;

void initialize() {
  orig_cxa_throw = (CxaThrowType)dlsym(RTLD_NEXT, "__cxa_throw");
  orig_cxa_begin_catch =
    (CxaBeginCatchType)dlsym(RTLD_NEXT, "__cxa_begin_catch");
  orig_cxa_rethrow =
    (CxaRethrowType)dlsym(RTLD_NEXT, "__cxa_rethrow");
  orig_cxa_end_catch = (CxaEndCatchType)dlsym(RTLD_NEXT, "__cxa_end_catch");
  // Mangled name for std::rethrow_exception
  // TODO(tudorb): Dicey, as it relies on the fact that std::exception_ptr
  // is typedef'ed to a type in namespace __exception_ptr
  orig_rethrow_exception =
    (RethrowExceptionType)dlsym(
        RTLD_NEXT,
        "_ZSt17rethrow_exceptionNSt15__exception_ptr13exception_ptrE");

  if (!orig_cxa_throw || !orig_cxa_begin_catch || !orig_cxa_rethrow ||
      !orig_cxa_end_catch || !orig_rethrow_exception) {
    abort();  // what else can we do?
  }
}

}  // namespace

// This function is exported and may be found via dlsym(RTLD_NEXT, ...)
extern "C" StackTraceStack* getExceptionStackTraceStack() {
  return invalid ? nullptr : &caughtExceptions;
}

namespace {

// Make sure we're counting stack frames correctly, don't inline.
FOLLY_NOINLINE void addActiveException();

void addActiveException() {
  pthread_once(&initialized, initialize);
  // Capture stack trace
  if (!invalid) {
    if (!activeExceptions.pushCurrent()) {
      activeExceptions.clear();
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

}  // namespace

namespace __cxxabiv1 {

void __cxa_throw(void* thrownException,
                 std::type_info* type,
                 void (*destructor)(void*)) {
  addActiveException();
  orig_cxa_throw(thrownException, type, destructor);
}

void __cxa_rethrow() {
  // __cxa_rethrow leaves the current exception on the caught stack,
  // and __cxa_begin_catch recognizes that case.  We could do the same, but
  // we'll implement something simpler (and slower): we pop the exception from
  // the caught stack, and push it back onto the active stack; this way, our
  // implementation of __cxa_begin_catch doesn't have to do anything special.
  moveTopException(caughtExceptions, activeExceptions);
  orig_cxa_rethrow();
}

void* __cxa_begin_catch(void *excObj) {
  // excObj is a pointer to the unwindHeader in __cxa_exception
  moveTopException(activeExceptions, caughtExceptions);
  return orig_cxa_begin_catch(excObj);
}

void __cxa_end_catch() {
  if (!invalid) {
    __cxa_exception* top = __cxa_get_globals_fast()->caughtExceptions;
    // This is gcc specific and not specified in the ABI:
    // abs(handlerCount) is the number of active handlers, it's negative
    // for rethrown exceptions and positive (always 1) for regular exceptions.
    // In the rethrow case, we've already popped the exception off the
    // caught stack, so we don't do anything here.
    if (top->handlerCount == 1) {
      if (!caughtExceptions.pop()) {
        activeExceptions.clear();
        invalid = true;
      }
    }
  }
  orig_cxa_end_catch();
}

}  // namespace __cxxabiv1

namespace std {

void rethrow_exception(std::exception_ptr ep) {
  addActiveException();
  orig_rethrow_exception(ep);
}

}  // namespace std


namespace {

struct Initializer {
  Initializer() {
    try {
      ::folly::exception_tracer::installHandlers();
    } catch (...) {
    }
  }
};

Initializer initializer;

}  // namespace
