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

#include <cerrno>

#include <folly/CPortability.h>
#include <folly/Portability.h>

#if !defined(_WIN32)

#include <unistd.h>

#include <sys/syscall.h>

#if defined(__APPLE__)
#define FOLLY_SYS_gettid SYS_thread_selfid
#elif defined(SYS_gettid)
#define FOLLY_SYS_gettid SYS_gettid
#else
#define FOLLY_SYS_gettid __NR_gettid
#endif

#endif

namespace folly {
namespace detail {

//  linux_syscall
//
//  Follows the interface of syscall(2), as described for linux. Other platforms
//  offer compatible interfaces. Defined for all platforms, whereas syscall(2)
//  is only defined on some platforms and is only exported by unistd.h on those
//  platforms which have unistd.h.
//
//  Note: This uses C++ variadic args while syscall(2) uses C variadic args,
//  which have different signatures and which use different calling conventions.
//
//  Note: Some syscall numbers are specified by POSIX but some are specific to
//  each platform and vary by operating system and architecture. Caution is
//  required.
//
//  mimic: syscall(2), linux
template <typename... A>
FOLLY_ERASE long linux_syscall(long number, A... a) {
#if defined(_WIN32) || (defined(__EMSCRIPTEN__) && !defined(syscall))
  errno = ENOSYS;
  return -1;
#else
  // syscall is deprecated under iOS >= 10.0
  FOLLY_PUSH_WARNING
  FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
  return syscall(number, a...);
  FOLLY_POP_WARNING
#endif
}

} // namespace detail
} // namespace folly
