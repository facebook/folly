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

#ifndef FOLLY_DETAIL_UNCAUGHTEXCEPTIONCOUNTER_H_
#define FOLLY_DETAIL_UNCAUGHTEXCEPTIONCOUNTER_H_

#include <exception>

#if defined(__GNUG__) || defined(__CLANG__)
#define FOLLY_EXCEPTION_COUNT_USE_CXA_GET_GLOBALS
namespace __cxxabiv1 {
// forward declaration (originally defined in unwind-cxx.h from from libstdc++)
struct __cxa_eh_globals;
// declared in cxxabi.h from libstdc++-v3
extern "C" __cxa_eh_globals* __cxa_get_globals() noexcept;
}
#elif defined(_MSC_VER) && (_MSC_VER >= 1400) // MSVC++ 8.0 or greater
#define FOLLY_EXCEPTION_COUNT_USE_GETPTD
// forward declaration (originally defined in mtdll.h from MSVCRT)
struct _tiddata;
extern "C" _tiddata* _getptd(); // declared in mtdll.h from MSVCRT
#else
// Raise an error when trying to use this on unsupported platforms.
#error "Unsupported platform, don't include this header."
#endif


namespace folly { namespace detail {

/**
 * Used to check if a new uncaught exception was thrown by monitoring the
 * number of uncaught exceptions.
 *
 * Usage:
 *  - create a new UncaughtExceptionCounter object
 *  - call isNewUncaughtException() on the new object to check if a new
 *    uncaught exception was thrown since the object was created
 */
class UncaughtExceptionCounter {
 public:
  UncaughtExceptionCounter()
    : exceptionCount_(getUncaughtExceptionCount()) {}

  UncaughtExceptionCounter(const UncaughtExceptionCounter& other)
    : exceptionCount_(other.exceptionCount_) {}

  bool isNewUncaughtException() noexcept {
    return getUncaughtExceptionCount() > exceptionCount_;
  }

 private:
  int getUncaughtExceptionCount() noexcept;

  int exceptionCount_;
};

/**
 * Returns the number of uncaught exceptions.
 *
 * This function is based on Evgeny Panasyuk's implementation from here:
 * http://fburl.com/15190026
 */
inline int UncaughtExceptionCounter::getUncaughtExceptionCount() noexcept {
#if defined(FOLLY_EXCEPTION_COUNT_USE_CXA_GET_GLOBALS)
  // __cxa_get_globals returns a __cxa_eh_globals* (defined in unwind-cxx.h).
  // The offset below returns __cxa_eh_globals::uncaughtExceptions.
  return *(reinterpret_cast<unsigned int*>(static_cast<char*>(
      static_cast<void*>(__cxxabiv1::__cxa_get_globals())) + sizeof(void*)));
#elif defined(FOLLY_EXCEPTION_COUNT_USE_GETPTD)
  // _getptd() returns a _tiddata* (defined in mtdll.h).
  // The offset below returns _tiddata::_ProcessingThrow.
  return *(reinterpret_cast<int*>(static_cast<char*>(
      static_cast<void*>(_getptd())) + sizeof(void*) * 28 + 0x4 * 8));
#endif
}

}} // namespaces

#endif /* FOLLY_DETAIL_UNCAUGHTEXCEPTIONCOUNTER_H_ */
