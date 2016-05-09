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

#include <folly/portability/Time.h>

#if !FOLLY_HAVE_CLOCK_GETTIME
#if __MACH__
#include <errno.h>
#include <mach/mach_time.h>

static const mach_timebase_info_data_t* tbInfo() {
  static auto info = [] {
    static mach_timebase_info_data_t info;
    return (mach_timebase_info(&info) == KERN_SUCCESS) ? &info : nullptr;
  }();
  return info;
}

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
#elif defined(_WIN32)
#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>

#include <folly/portability/Windows.h>

static constexpr size_t kNsPerSec = 1000000000;

extern "C" int clock_getres(clockid_t clock_id, struct timespec* res) {
  if (!res) {
    errno = EFAULT;
    return -1;
  }

  switch (clock_id) {
    case CLOCK_MONOTONIC: {
      LARGE_INTEGER freq;
      if (!QueryPerformanceFrequency(&freq)) {
        errno = EINVAL;
        return -1;
      }

      res->tv_sec = 0;
      res->tv_nsec = (long)((kNsPerSec + (freq.QuadPart >> 1)) / freq.QuadPart);
      if (res->tv_nsec < 1) {
        res->tv_nsec = 1;
      }

      return 0;
    }

    case CLOCK_REALTIME:
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID: {
      DWORD adj, timeIncrement;
      BOOL adjDisabled;
      if (!GetSystemTimeAdjustment(&adj, &timeIncrement, &adjDisabled)) {
        errno = EINVAL;
        return -1;
      }

      res->tv_sec = 0;
      res->tv_nsec = timeIncrement * 100;
      return 0;
    }

    default:
      errno = EINVAL;
      return -1;
  }
}

extern "C" int clock_gettime(clockid_t clock_id, struct timespec* tp) {
  if (!tp) {
    errno = EFAULT;
    return -1;
  }

  const auto ftToUint = [](FILETIME ft) -> uint64_t {
    ULARGE_INTEGER i;
    i.HighPart = ft.dwHighDateTime;
    i.LowPart = ft.dwLowDateTime;
    return i.QuadPart;
  };
  const auto timeToTimespec = [](timespec* tp, uint64_t t) -> int {
    constexpr size_t k100NsPerSec = kNsPerSec / 100;

    // The filetimes t is based on are represented in
    // 100ns's. (ie. a value of 4 is 400ns)
    tp->tv_sec = t / k100NsPerSec;
    tp->tv_nsec = ((long)(t % k100NsPerSec)) * 100;
    return 0;
  };

  FILETIME createTime, exitTime, kernalTime, userTime;
  switch (clock_id) {
    case CLOCK_REALTIME: {
      constexpr size_t kDeltaEpochIn100NS = 116444736000000000ULL;

      GetSystemTimeAsFileTime(&createTime);
      return timeToTimespec(tp, ftToUint(createTime) - kDeltaEpochIn100NS);
    }
    case CLOCK_PROCESS_CPUTIME_ID: {
      if (!GetProcessTimes(
              GetCurrentProcess(),
              &createTime,
              &exitTime,
              &kernalTime,
              &userTime)) {
        errno = EINVAL;
        return -1;
      }

      return timeToTimespec(tp, ftToUint(kernalTime) + ftToUint(userTime));
    }
    case CLOCK_THREAD_CPUTIME_ID: {
      if (!GetThreadTimes(
              GetCurrentThread(),
              &createTime,
              &exitTime,
              &kernalTime,
              &userTime)) {
        errno = EINVAL;
        return -1;
      }

      return timeToTimespec(tp, ftToUint(kernalTime) + ftToUint(userTime));
    }
    case CLOCK_MONOTONIC: {
      LARGE_INTEGER fl, cl;
      if (!QueryPerformanceFrequency(&fl) || !QueryPerformanceCounter(&cl)) {
        errno = EINVAL;
        return -1;
      }

      int64_t freq = fl.QuadPart;
      int64_t counter = cl.QuadPart;
      tp->tv_sec = counter / freq;
      tp->tv_nsec = (long)(((counter % freq) * kNsPerSec + (freq >> 1)) / freq);
      if (tp->tv_nsec >= kNsPerSec) {
        tp->tv_sec++;
        tp->tv_nsec -= kNsPerSec;
      }

      return 0;
    }

    default:
      errno = EINVAL;
      return -1;
  }
}
#else
#error No clock_gettime(3) compatibility wrapper available for this platform.
#endif
#endif

#ifdef _WIN32
#include <iomanip>
#include <sstream>

#include <folly/portability/Windows.h>

extern "C" {
char* asctime_r(const tm* tm, char* buf) {
  char tmpBuf[64];
  if (asctime_s(tmpBuf, tm)) {
    return nullptr;
  }
  // Nothing we can do if the buff is to small :(
  return strcpy(buf, tmpBuf);
}

char* ctime_r(const time_t* t, char* buf) {
  char tmpBuf[64];
  if (ctime_s(tmpBuf, 64, t)) {
    return nullptr;
  }
  // Nothing we can do if the buff is to small :(
  return strcpy(buf, tmpBuf);
}

tm* gmtime_r(const time_t* t, tm* res) {
  if (!gmtime_s(res, t)) {
    return res;
  }
  return nullptr;
}

tm* localtime_r(const time_t* t, tm* o) {
  if (!localtime_s(o, t)) {
    return o;
  }
  return nullptr;
}

int nanosleep(const struct timespec* request, struct timespec* remain) {
  Sleep((DWORD)((request->tv_sec * 1000) + (request->tv_nsec / 1000000)));
  remain->tv_nsec = 0;
  remain->tv_sec = 0;
  return 0;
}

char* strptime(const char* __restrict s,
               const char* __restrict f,
               struct tm* __restrict tm) {
  // Isn't the C++ standard lib nice? std::get_time is defined such that its
  // format parameters are the exact same as strptime. Of course, we have to
  // create a string stream first, and imbue it with the current C locale, and
  // we also have to make sure we return the right things if it fails, or
  // if it succeeds, but this is still far simpler an implementation than any
  // of the versions in any of the C standard libraries.
  std::istringstream input(s);
  input.imbue(std::locale(setlocale(LC_ALL, nullptr)));
  input >> std::get_time(tm, f);
  if (input.fail()) {
    return nullptr;
  }
  return const_cast<char*>(s + input.tellg());
}
}
#endif
