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

#include <folly/system/ThreadId.h>

#include <folly/portability/PThread.h>
#include <folly/portability/SysSyscall.h>
#include <folly/portability/Unistd.h>
#include <folly/portability/Windows.h>

#ifdef __XROS__
#include <xr/execution/accessors.h> // @manual
#endif

namespace folly {

uint64_t getCurrentThreadID() {
#if __APPLE__
  return uint64_t(pthread_mach_thread_np(pthread_self()));
#elif defined(_WIN32)
  return uint64_t(GetCurrentThreadId());
#elif defined(__XROS__)
  return uint64_t(xr_execution_get_id());
#else
  return uint64_t(pthread_self());
#endif
}

uint64_t getOSThreadID() {
#if __APPLE__
  uint64_t tid;
  pthread_threadid_np(nullptr, &tid);
  return tid;
#elif defined(_WIN32)
  return uint64_t(GetCurrentThreadId());
#elif defined(__FreeBSD__)
  long tid;
  thr_self(&tid);
  return uint64_t(tid);
#elif defined(__XROS__)
  return uint64_t(xr_execution_get_id());
#else
  return uint64_t(syscall(FOLLY_SYS_gettid));
#endif
}
} // namespace folly
