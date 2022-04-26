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

#include <folly/portability/SysMembarrier.h>

#include <cassert>
#include <cerrno>

#include <folly/Portability.h>
#include <folly/portability/SysSyscall.h>

namespace folly {
namespace detail {

namespace {

constexpr long linux_syscall_nr_(long nr, long def) {
  return nr == -1 ? def : nr;
}

//  __NR_membarrier or -1; always defined as v.s. __NR_membarrier
#if defined(__NR_membarrier)
constexpr long linux_syscall_nr_membarrier_ = __NR_membarrier;
#else
constexpr long linux_syscall_nr_membarrier_ = -1;
#endif

//  __NR_membarrier with hardcoded fallback where available or -1
constexpr long linux_syscall_nr_membarrier =
    kIsArchAmd64 && !kIsMobile && kIsLinux //
    ? linux_syscall_nr_(linux_syscall_nr_membarrier_, 324)
    : -1;

//  linux_membarrier_cmd
//
//  Backport from the linux header, since older versions of the header may
//  define the enum but not all of the enum constants that we require.
//
//  mimic: membarrier_cmd, linux/membarrier.h
enum linux_membarrier_cmd {
  MEMBARRIER_CMD_QUERY = 0,
  MEMBARRIER_CMD_PRIVATE_EXPEDITED = (1 << 3),
  MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED = (1 << 4),
};

FOLLY_ERASE int call_membarrier(int cmd, unsigned int flags = 0) {
  if (linux_syscall_nr_membarrier < 0) {
    errno = ENOSYS;
    return -1;
  }
  return linux_syscall(linux_syscall_nr_membarrier, cmd, flags);
}

} // namespace

bool sysMembarrierPrivateExpeditedAvailable() {
  constexpr auto flags = 0 //
      | MEMBARRIER_CMD_PRIVATE_EXPEDITED //
      | MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED;

  auto const r = call_membarrier(MEMBARRIER_CMD_QUERY);
  return r != -1 && (r & flags) == flags;
}

int sysMembarrierPrivateExpedited() {
  if (0 == call_membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED)) {
    return 0;
  }
  switch (errno) {
    case EINVAL:
    case ENOSYS:
      return -1;
  }
  assert(errno == EPERM);
  if (-1 == call_membarrier(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED)) {
    return -1;
  }
  return call_membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED);
}

} // namespace detail
} // namespace folly
