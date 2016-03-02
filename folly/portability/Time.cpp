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

#ifdef _WIN32
#include <iomanip>
#include <sstream>

#include <Windows.h>

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
  if (ctime_s(tmpBuf, t)) {
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
  Sleep((DWORD)((request->tv_sec * 1000) + (request->tv_nsec / 1000000));
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
