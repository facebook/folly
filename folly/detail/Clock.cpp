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

#include <folly/detail/Clock.h>

#if __MACH__
#include <errno.h>
#include <mach/mach_time.h>

namespace {

const mach_timebase_info_data_t* tbInfo() {
  static auto info = [] {
    static mach_timebase_info_data_t info;
    return (mach_timebase_info(&info) == KERN_SUCCESS) ? &info : nullptr;
  }();
  return info;
};

}  // anonymous namespace

int clock_gettime(clockid_t clk_id, struct timespec* ts) {
  auto tb_info = tbInfo();
  if (tb_info == nullptr) {
    errno = EINVAL;
    return -1;
  }

  uint64_t now_ticks = mach_absolute_time();
  uint64_t now_ns = (now_ticks * tb_info->numer) / tb_info->denom;
  ts->tv_sec = now_ns / 1000000000;
  ts->tv_nsec = now_ns % 1000000000;

  return 0;
}

int clock_getres(clockid_t clk_id, struct timespec* ts) {
  auto tb_info = tbInfo();
  if (tb_info == nullptr) {
    errno = EINVAL;
    return -1;
  }

  ts->tv_sec = 0;
  ts->tv_nsec = tb_info->numer / tb_info->denom;

  return 0;
}
#elif defined(_MSC_VER)
// The MSVC version has been extracted from the pthreads implemenation here:
// https://github.com/songdongsheng/libpthread
// Copyright(c) 2011, Dongsheng Song <songdongsheng@live.cn>
//
// It is under the Apache License Version 2.0, just as the rest of the file is.
// It has been mostly stripped down to what we have.

#include <WinSock2.h>

#define DELTA_EPOCH_IN_100NS    INT64_C(116444736000000000)
#define POW10_7     INT64_C(10000000)
#define POW10_9     INT64_C(1000000000)

int clock_getres(clockid_t clock_id, struct timespec *res)
{
  switch (clock_id) {
    case CLOCK_MONOTONIC:
    {
      LARGE_INTEGER pf;

      if (QueryPerformanceFrequency(&pf) == 0)
        return -1;

      res->tv_sec = 0;
      res->tv_nsec = (int)((POW10_9 + (pf.QuadPart >> 1)) / pf.QuadPart);
      if (res->tv_nsec < 1)
        res->tv_nsec = 1;

      return 0;
    }

    case CLOCK_REALTIME:
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID:
    {
      DWORD   timeAdjustment, timeIncrement;
      BOOL    isTimeAdjustmentDisabled;

      (void)GetSystemTimeAdjustment(
        &timeAdjustment,
        &timeIncrement,
        &isTimeAdjustmentDisabled
      );
      res->tv_sec = 0;
      res->tv_nsec = timeIncrement * 100;

      return 0;
    }
    default:
      break;
  }

  return -1;
}

int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
  unsigned __int64 t;
  LARGE_INTEGER pf, pc;
  union {
    unsigned __int64 u64;
    FILETIME ft;
  }  ct, et, kt, ut;

  switch (clock_id) {
    case CLOCK_REALTIME:
    {
      GetSystemTimeAsFileTime(&ct.ft);
      t = ct.u64 - DELTA_EPOCH_IN_100NS;
      tp->tv_sec = t / POW10_7;
      tp->tv_nsec = ((int)(t % POW10_7)) * 100;

      return 0;
    }

    case CLOCK_MONOTONIC:
    {
      if (QueryPerformanceFrequency(&pf) == 0)
        return -1;

      if (QueryPerformanceCounter(&pc) == 0)
        return -1;

      tp->tv_sec = pc.QuadPart / pf.QuadPart;
      tp->tv_nsec = (int)(
        ((pc.QuadPart % pf.QuadPart) * POW10_9 + (pf.QuadPart >> 1)) /
        pf.QuadPart
      );
      if (tp->tv_nsec >= POW10_9) {
        tp->tv_sec++;
        tp->tv_nsec -= POW10_9;
      }

      return 0;
    }

    case CLOCK_PROCESS_CPUTIME_ID:
    {
      if (0 == GetProcessTimes(GetCurrentProcess(),
                               &ct.ft, &et.ft, &kt.ft, &ut.ft)) {
        return -1;
      }
      t = kt.u64 + ut.u64;
      tp->tv_sec = t / POW10_7;
      tp->tv_nsec = ((int)(t % POW10_7)) * 100;

      return 0;
    }

    case CLOCK_THREAD_CPUTIME_ID:
    {
      if (0 == GetThreadTimes(GetCurrentThread(),
                              &ct.ft, &et.ft, &kt.ft, &ut.ft)) {
        return -1;
      }
      t = kt.u64 + ut.u64;
      tp->tv_sec = t / POW10_7;
      tp->tv_nsec = ((int)(t % POW10_7)) * 100;

      return 0;
    }

    default:
      break;
  }

  return -1;
}
#elif defined(__CYGWIN__) || defined(__MINGW__)
// using winpthreads from mingw-w64
// <pthreads_time.h> has clock_gettime and friends
// make sure to include <pthread.h> as well for typedefs of timespec/etc
#else
#error No clock_gettime(2) compatibility wrapper available for this platform.
#endif
