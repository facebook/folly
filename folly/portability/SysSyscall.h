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
#include <cstdint>

#include <folly/CPortability.h>
#include <folly/Portability.h>

#if !defined(_WIN32)

#include <unistd.h>

#include <sys/syscall.h>

#if defined(__APPLE__)
#define FOLLY_SYS_gettid SYS_thread_selfid
#elif defined(__EMSCRIPTEN__)
#define FOLLY_SYS_gettid 0
#elif defined(SYS_gettid)
#define FOLLY_SYS_gettid SYS_gettid
#else
#define FOLLY_SYS_gettid __NR_gettid
#endif

#endif

//  FOLLY_DETAIL_LINUX_SYSCALL_INLINE_2
//
//  Performs a two-argument linux syscall, emitting the syscall instruction at
//  the point of use. Assigns to res, a long lvalue, the raw kernel result: the
//  return value on success, or the negated errno on failure. Unlike
//  linux_syscall it neither sets errno nor maps failure to -1.
//
//  Prefer linux_syscall. Reach for this only where the out-of-line call into
//  libc's syscall(2) is itself a correctness problem, rather than merely an
//  overhead. The motivating case is clone3 with CLONE_VM | CLONE_VFORK and no
//  child stack: the child resumes on the parent's stack pointer, so anything
//  it pushes must stay below the frames through which the suspended parent
//  will return. A libc wrapper leaves its own return address inside exactly
//  that region, and the child overwrites it as soon as it calls anything. libc
//  implements vfork as a frameless assembly stub for the same reason.
//
//  Expanding this macro where no audited stub exists is a compile error,
//  deliberately. A silent fallback would leave a newly supported architecture
//  permanently on the slower path with nothing to signal it. The error fires
//  at the point of use, so merely including this header stays valid
//  everywhere, including on non-linux platforms.
//
//  To add an architecture: add a stub below, and confirm from the disassembly
//  of a caller that the syscall instruction is reached with no intervening
//  call frame.
#if defined(__linux__) && defined(__x86_64__)

#define FOLLY_DETAIL_LINUX_SYSCALL_INLINE_2(res, number, arg0, arg1) \
  do {                                                               \
    __asm__ __volatile__(                                            \
        "syscall"                                                    \
        : "=a"(res)                                                  \
        : "0"((long)(number)), "D"((long)(arg0)), "S"((long)(arg1))  \
        : "rcx", "r11", "memory", "cc");                             \
  } while (0)

#elif defined(__linux__) && defined(__aarch64__)

#define FOLLY_DETAIL_LINUX_SYSCALL_INLINE_2(res, number, arg0, arg1) \
  do {                                                               \
    register long _folly_x8 __asm__("x8") = (long)(number);          \
    register long _folly_x0 __asm__("x0") = (long)(arg0);            \
    register long _folly_x1 __asm__("x1") = (long)(arg1);            \
    __asm__ __volatile__(                                            \
        "svc #0"                                                     \
        : "+r"(_folly_x0)                                            \
        : "r"(_folly_x8), "r"(_folly_x1)                             \
        : "memory", "cc");                                           \
    (res) = _folly_x0;                                               \
  } while (0)

#else

#define FOLLY_DETAIL_LINUX_SYSCALL_INLINE_2(res, number, arg0, arg1) \
  static_assert(                                                     \
      false,                                                         \
      "FOLLY_DETAIL_LINUX_SYSCALL_INLINE_2 has no stub for this "    \
      "architecture. Add one in folly/portability/SysSyscall.h.")

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
FOLLY_ERASE long linux_syscall(
    [[maybe_unused]] long number, [[maybe_unused]] A... a) {
#if defined(_WIN32) || (defined(__EMSCRIPTEN__) && !defined(syscall)) || \
    FOLLY_APPLE_TVOS
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

#if defined(__linux__)

//  clone_args
//
//  The prefix of struct clone_args (linux uapi) up to and including .cgroup,
//  i.e. CLONE_ARGS_SIZE_VER2. Spelled out here rather than taken from
//  <linux/sched.h> so that the code still builds against kernel headers
//  predating clone3.
struct alignas(8) clone_args {
  uint64_t flags;
  uint64_t pidfd;
  uint64_t child_tid;
  uint64_t parent_tid;
  uint64_t exit_signal;
  uint64_t stack;
  uint64_t stack_size;
  uint64_t tls;
  uint64_t set_tid;
  uint64_t set_tid_size;
  uint64_t cgroup;
};
static_assert(alignof(clone_args) == 8, "linux uapi __aligned_u64");
static_assert(sizeof(clone_args) == 88, "CLONE_ARGS_SIZE_VER2");

#endif

} // namespace detail
} // namespace folly
