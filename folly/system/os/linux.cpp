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

#include <folly/system/os/linux.h>

#include <folly/portability/SysSyscall.h>

#if defined(__linux__) && defined(SYS_openat2)
#include <linux/openat2.h>
#endif

namespace folly {

long linux_syscall_openat2(
    int const dirfd,
    char const* const pathname,
    struct open_how const* const how) {
#if defined(__linux__) && defined(SYS_openat2)
  constexpr long no_openat2 = SYS_openat2;
  constexpr size_t sz_how = sizeof(struct open_how);
#else
  constexpr long no_openat2 = -1;
  constexpr size_t sz_how = 0;
#endif
  return detail::linux_syscall(no_openat2, dirfd, pathname, how, sz_how);
}

} // namespace folly
