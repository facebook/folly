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

// A clone of the relevant parts of unwind-cxx.h from libstdc++
// The layout of these structures is defined by the ABI.

#include <exception>
#include <typeinfo>

#include <folly/debugging/exception_tracer/Compatibility.h>

#if FOLLY_HAS_EXCEPTION_TRACER

#include <unwind.h>

namespace __cxxabiv1 {

struct __cxa_exception {
// Unlike other implementations, GCC's libsupc++ doesn't have this code.
// See gcc/libstdc++-v3/libsupc++/unwind-cxx.h
#if !defined(__GLIBCXX__)
#if defined(__LP64__) || defined(_WIN64) || defined(_LIBCXXABI_ARM_EHABI)
  // Now _Unwind_Exception is marked with __attribute__((aligned)),
  // which implies __cxa_exception is also aligned. Insert padding
  // in the beginning of the struct, rather than before unwindHeader.
  void* reserve;

  // This is a new field to support C++11 exception_ptr.
  // For binary compatibility it is at the start of this
  // struct which is prepended to the object thrown in
  // __cxa_allocate_exception.
  size_t referenceCount;
#endif // defined(__LP64__) || defined(_WIN64) || defined(_LIBCXXABI_ARM_EHABI)
#endif // !defined(__GLIBCXX__)

  std::type_info* exceptionType;
  void (*exceptionDestructor)(void*);
  void (*unexpectedHandler)(); // std::unexpected_handler has been removed from
                               // C++17.
  std::terminate_handler terminateHandler;
  __cxa_exception* nextException;

  int handlerCount;
  int handlerSwitchValue;
  const char* actionRecord;
  const char* languageSpecificData;
  void* catchTemp;
  void* adjustedPtr;

  _Unwind_Exception unwindHeader;
};

struct __cxa_eh_globals {
  __cxa_exception* caughtExceptions;
  unsigned int uncaughtExceptions;
};

extern "C" {
__cxa_eh_globals* __cxa_get_globals(void) noexcept;
__cxa_eh_globals* __cxa_get_globals_fast(void) noexcept;
}

} // namespace __cxxabiv1

#endif // FOLLY_HAS_EXCEPTION_TRACER
