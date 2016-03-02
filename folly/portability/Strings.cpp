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

#include <folly/portability/Strings.h>

#ifdef _WIN32
#include <string.h>

extern "C" {
void bzero(void* s, size_t n) { memset(s, 0, n); }

int strcasecmp(const char* a, const char* b) { return _stricmp(a, b); }

int strncasecmp(const char* a, const char* b, size_t c) {
  return _strnicmp(a, b, c);
}
}
#endif
