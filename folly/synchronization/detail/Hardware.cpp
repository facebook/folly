/*
 * Copyright 2019-present Facebook, Inc.
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

#include <folly/synchronization/detail/Hardware.h>

#include <cassert>
#include <cstdlib>
#include <exception>
#include <utility>

#include <glog/logging.h>

#if FOLLY_X64 && defined(__RTM__)
#include <folly/lang/Assume.h>
#include <immintrin.h>
#define FOLLY_RTM_SUPPORT 1
#elif FOLLY_X64
#endif

#if FOLLY_RTM_SUPPORT
#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#elif defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace folly {
namespace hardware {

bool rtmEnabled() {
#if FOLLY_RTM_SUPPORT

#if defined(__GNUC__) || defined(__clang__)

  if (__get_cpuid_max(0, nullptr) < 7) {
    // very surprising, older than Core Duo!
    return false;
  }
  unsigned ax, bx, cx, dx;
  // CPUID EAX=7, ECX=0: Extended Features
  // EBX bit 11 -> RTM support
  __cpuid_count(7, 0, ax, bx, cx, dx);
  return ((bx >> 11) & 1) != 0;
#elif defined(_MSC_VER)
  // __cpuidex:
  // https://docs.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?view=vs-2019
  int cpui[4];
  __cpuid(cpui, 0);
  if (unsigned(cpui[0]) < 7) {
    return false;
  }
  __cpuidex(cpui, 7, 0);
  return ((cpui[1] >> 11) & 1) != 0;
#else
  return false;
#endif

#else // FOLLY_RTM_SUPPORT == 0
  return false;
#endif
}

static unsigned rtmBeginFunc() {
#if FOLLY_RTM_SUPPORT
  return _xbegin();
#else
  return kRtmDisabled;
#endif
}

static void rtmEndFunc() {
#if FOLLY_RTM_SUPPORT
  _xend();
#else
  CHECK(false) << "rtmEnd called without txn available";
#endif
}

static bool rtmTestFunc() {
#if FOLLY_RTM_SUPPORT
  return _xtest() != 0;
#else
  return 0;
#endif
}

#if FOLLY_RTM_SUPPORT
[[noreturn]] FOLLY_NOINLINE void rtmAbortFuncFailed() {
  // If we get here either status is too large or we weren't in a txn.
  // If we are actually in a transaction then assert, std::abort, or
  // _xabort will all end up aborting the txn.  If we're not in a txn
  // then we have done something very wrong.
  CHECK(false)
      << "rtmAbort called with an invalid status or without an active txn";
  folly::assume_unreachable();
}
#endif

[[noreturn]] static void rtmAbortFunc(unsigned status) {
#if FOLLY_RTM_SUPPORT
  // Manual case statement instead of using make_index_sequence so
  // that we can avoid any object lifetime ASAN interactions even
  // in non-optimized builds.  _xabort needs a compile-time constant
  // argument :(
  switch (status) {
    case 0:
      _xabort(0);
      break;
    case 1:
      _xabort(1);
      break;
    case 2:
      _xabort(2);
      break;
    case 3:
      _xabort(3);
      break;
    case 4:
      _xabort(4);
      break;
  }
  rtmAbortFuncFailed();
#else
  (void)status;
  std::terminate();
#endif
}

unsigned (*const rtmBegin)() = rtmBeginFunc;

void (*const rtmEnd)() = rtmEndFunc;

bool (*const rtmTest)() = rtmTestFunc;

void (*const rtmAbort)(unsigned) = rtmAbortFunc;
} // namespace hardware
} // namespace folly
