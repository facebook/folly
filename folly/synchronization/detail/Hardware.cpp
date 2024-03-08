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

#include <folly/synchronization/detail/Hardware.h>

#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <utility>

#include <boost/preprocessor/repetition/repeat.hpp>

#include <folly/CppAttributes.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Exception.h>

#if FOLLY_X64 && defined(__RTM__)
#include <immintrin.h>
#define FOLLY_RTM_SUPPORT 1
#else
#define FOLLY_RTM_SUPPORT 0
#endif

#if FOLLY_RTM_SUPPORT
#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#elif defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace folly {

static bool rtmEnabledImpl() {
#if !FOLLY_RTM_SUPPORT

  return false;

#elif defined(__GNUC__) || defined(__clang__)

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
}

bool rtmEnabled() {
  static std::atomic<int> result{-1};
  auto value = result.load(std::memory_order_relaxed);
  if (value < 0) {
    value = int(rtmEnabledImpl());
    result.store(value, std::memory_order_relaxed);
  }
  return value;
}

#if FOLLY_RTM_SUPPORT
#define FOLLY_RTM_DISABLED_NORETURN
#else
#define FOLLY_RTM_DISABLED_NORETURN [[noreturn]]
#endif

FOLLY_RTM_DISABLED_NORETURN static unsigned rtmBeginFunc() {
#if FOLLY_RTM_SUPPORT
  return _xbegin();
#else
  assume_unreachable();
#endif
}

FOLLY_RTM_DISABLED_NORETURN static void rtmEndFunc() {
#if FOLLY_RTM_SUPPORT
  _xend();
#else
  assume_unreachable();
#endif
}

FOLLY_RTM_DISABLED_NORETURN static bool rtmTestFunc() {
#if FOLLY_RTM_SUPPORT
  return _xtest() != 0;
#else
  assume_unreachable();
#endif
}

[[noreturn]] FOLLY_DISABLE_SANITIZERS static void rtmAbortFunc(
    [[maybe_unused]] uint8_t status) {
#if FOLLY_RTM_SUPPORT
  switch (status) {
#define FOLLY_RTM_ABORT_ONE(z, n, text) \
  case uint8_t(n):                      \
    _xabort(uint8_t(n));                \
    [[fallthrough]];
    BOOST_PP_REPEAT(256, FOLLY_RTM_ABORT_ONE, unused)
#undef FOLLY_RTM_ABORT_ONE
    default:
      terminate_with<std::runtime_error>("rtm not in transaction");
  }
#else
  assume_unreachable();
#endif
}

namespace detail {

unsigned rtmBeginDisabled() {
  return kRtmDisabled;
}
void rtmEndDisabled() {}
bool rtmTestDisabled() {
  return false;
}
[[noreturn]] void rtmAbortDisabled(uint8_t) {
  terminate_with<std::runtime_error>("rtm not enabled");
}

static void rewrite() {
  if (rtmEnabled()) {
    rtmBeginV.store(rtmBeginFunc, std::memory_order_relaxed);
    rtmEndV.store(rtmEndFunc, std::memory_order_relaxed);
    rtmTestV.store(rtmTestFunc, std::memory_order_relaxed);
    rtmAbortV.store(rtmAbortFunc, std::memory_order_relaxed);
  } else {
    rtmBeginV.store(rtmBeginDisabled, std::memory_order_relaxed);
    rtmEndV.store(rtmEndDisabled, std::memory_order_relaxed);
    rtmTestV.store(rtmTestDisabled, std::memory_order_relaxed);
    rtmAbortV.store(rtmAbortDisabled, std::memory_order_relaxed);
  }
}

unsigned rtmBeginVE() {
  rewrite();
  return rtmBeginV.load(std::memory_order_relaxed)();
}
void rtmEndVE() {
  rewrite();
  rtmEndV.load(std::memory_order_relaxed)();
}
bool rtmTestVE() {
  rewrite();
  return rtmTestV.load(std::memory_order_relaxed)();
}
void rtmAbortVE(uint8_t status) {
  rewrite();
  rtmAbortV.load(std::memory_order_relaxed)(status);
}

std::atomic<unsigned (*)()> rtmBeginV{rtmBeginVE};
std::atomic<void (*)()> rtmEndV{rtmEndVE};
std::atomic<bool (*)()> rtmTestV{rtmTestVE};
std::atomic<void (*)(uint8_t)> rtmAbortV{rtmAbortVE};

} // namespace detail

} // namespace folly
