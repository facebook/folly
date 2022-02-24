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

#include <stdint.h>
#include <time.h>

#include <folly/portability/Config.h>

#if defined(__MACH__) && defined(__CLOCK_AVAILABILITY)
// CMake might not look for clock_gettime in the <time.h> from the selected SDK
#ifdef FOLLY_HAVE_CLOCK_GETTIME
#undef FOLLY_HAVE_CLOCK_GETTIME
#endif

// If __CLOCK_AVAILABILITY is defined, all clock APIs are declared
#define FOLLY_HAVE_CLOCK_GETTIME 1

// Force use of the folly_* versions to avoid NULL weak symbol at runtime case
#define clock_gettime folly_clock_gettime
#define clock_getres folly_clock_getres

#endif

#if !FOLLY_HAVE_CLOCK_GETTIME
// These symbols and type are not defined if we haven't found clock_gettime yet
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3

typedef uint8_t clockid_t;

// Force use of the folly_* versions as a fallback
#define clock_gettime folly_clock_gettime
#define clock_getres folly_clock_getres

#endif

extern "C" int folly_clock_gettime(clockid_t clk_id, struct timespec* ts);
extern "C" int folly_clock_getres(clockid_t clk_id, struct timespec* ts);

#ifdef _WIN32
#define TM_YEAR_BASE (1900)

extern "C" {
char* asctime_r(const tm* tm, char* buf);
char* ctime_r(const time_t* t, char* buf);
tm* gmtime_r(const time_t* t, tm* res);
tm* localtime_r(const time_t* t, tm* o);
int nanosleep(const struct timespec* request, struct timespec* remain);
char* strptime(
    const char* __restrict buf,
    const char* __restrict fmt,
    struct tm* __restrict tm);
time_t timelocal(tm* tm);
time_t timegm(tm* tm);
}
#endif
