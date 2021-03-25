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

#pragma once

#ifndef _WIN32

#include <sys/time.h>

// win32 #defines timezone; this avoids collision
using folly_port_struct_timezone = struct timezone;

#else

// Someone decided this was a good place to define timeval.....
#include <folly/portability/Windows.h>

struct folly_port_struct_timezone_ {
  int tz_minuteswest;
  int tz_dsttime;
};
using folly_port_struct_timezone = struct folly_port_struct_timezone_;

extern "C" {

// We use folly_port_struct_timezone due to issues with #defines on Windows
// platforms.
// The python 3 headers `#define timezone _timezone` on Windows. `_timezone` is
// a global field that contains information on the current timezone.
// As such "timezone" is not a good name to use inside of C/C++ code on
// Windows.  Instead users should use folly_port_struct_timezone instead.
int gettimeofday(timeval* tv, folly_port_struct_timezone*);
void timeradd(timeval* a, timeval* b, timeval* res);
void timersub(timeval* a, timeval* b, timeval* res);
}

#endif
