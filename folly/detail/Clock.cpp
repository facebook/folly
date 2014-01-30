/*
 * Copyright 2014 Facebook, Inc.
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

#include "folly/detail/Clock.h"

#if __MACH__
#include <errno.h>
#include <mach/mach_time.h>

static mach_timebase_info_data_t tb_info;
static bool tb_init = mach_timebase_info(&tb_info) == KERN_SUCCESS;

int clock_gettime(clockid_t clk_id, struct timespec* ts) {
  if (!tb_init) {
    errno = EINVAL;
    return -1;
  }

  uint64_t now_ticks = mach_absolute_time();
  uint64_t now_ns = (now_ticks * tb_info.numer) / tb_info.denom;
  ts->tv_sec = now_ns / 1000000000;
  ts->tv_nsec = now_ns % 1000000000;

  return 0;
}

int clock_getres(clockid_t clk_id, struct timespec* ts) {
  if (!tb_init) {
    errno = EINVAL;
    return -1;
  }

  ts->tv_sec = 0;
  ts->tv_nsec = tb_info.numer / tb_info.denom;

  return 0;
}
#else
#error No clock_gettime(2) compatibility wrapper available for this platform.
#endif
