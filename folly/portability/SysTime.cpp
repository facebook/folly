/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/portability/SysTime.h>

#ifdef _WIN32
#include <cstdint>
#include <Windows.h>

extern "C" int gettimeofday(timeval* tv, timezone*) {
  constexpr auto posixWinFtOffset = 116444736000000000ULL;

  if (tv) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t ns = *(uint64_t*)&ft;
    tv->tv_usec = (long)((ns / 10ULL) % 1000000ULL);
    tv->tv_sec = (long)((ns - posixWinFtOffset) / 10000000ULL);
  }

  return 0;
}
#endif
