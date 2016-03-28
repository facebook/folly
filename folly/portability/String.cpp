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

#include <folly/portability/String.h>

#if !FOLLY_HAVE_MEMRCHR || defined(_WIN32) || defined(__APPLE__)
extern "C" void* memrchr(const void* s, int c, size_t n) {
  for (auto p = ((const char*)s) + n - 1; p >= (const char*)s; p--) {
    if (*p == (char)c) {
      return (void*)p;
    }
  }
  return nullptr;
}
#endif

#ifdef _WIN32
#include <stdlib.h>

extern "C" {
char* strndup(const char* a, size_t len) {
  auto neededLen = strlen(a);
  if (neededLen > len) {
    neededLen = len - 1;
  }
  char* buf = (char*)malloc((neededLen + 1) * sizeof(char));
  memcpy(buf, a, neededLen);
  buf[neededLen] = '\0';
  return buf;
}

char* strtok_r(char* str, char const* delim, char** ctx) {
  return strtok_s(str, delim, ctx);
}
}
#endif
