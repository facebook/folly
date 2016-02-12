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

#ifndef FOLLY_DETAIL_CLOCK_H_
#define FOLLY_DETAIL_CLOCK_H_

#include <ctime>
#include <cstdint>

#include <folly/Portability.h>

#if FOLLY_HAVE_CLOCK_GETTIME
#error This should only be used as a workaround for platforms \
          that do not support clock_gettime(2).
#endif

/* For windows, we'll use pthread's time implementations */
#if defined(__CYGWIN__) || defined(__MINGW__)
#include <pthread.h>
#include <pthread_time.h>
#else
typedef uint8_t clockid_t;
#define CLOCK_REALTIME 0
#ifdef _MSC_VER
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#endif

int clock_gettime(clockid_t clk_id, struct timespec* ts);
int clock_getres(clockid_t clk_id, struct timespec* ts);
#endif

#endif /* FOLLY_DETAIL_CLOCK_H_ */
