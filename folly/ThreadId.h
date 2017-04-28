/*
 * Copyright 2017 Facebook, Inc.
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

#pragma once

#include <cstdint>

#include <folly/portability/PThread.h>
#include <folly/portability/Windows.h>

namespace folly {

inline uint64_t getCurrentThreadID() {
#if __APPLE__
  return uint64_t(pthread_mach_thread_np(pthread_self()));
#elif _WIN32
  return uint64_t(GetCurrentThreadId());
#else
  return uint64_t(pthread_self());
#endif
}
}
