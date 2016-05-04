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

#include <folly/portability/Stdlib.h>

#ifdef _WIN32
#include <cstring>
#include <errno.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/SysStat.h>
#include <folly/portability/Windows.h>

extern "C" {
char* mktemp(char* tn) { return _mktemp(tn); }

// While yes, this is for a directory, due to this being windows,
// a file and directory can't have the same name, resulting in this
// still working just fine.
char* mkdtemp(char* tn) {
  char* ptr = nullptr;
  auto len = strlen(ptr);
  int ret = 0;
  do {
    strcpy(tn + len - 6, "XXXXXX");
    ptr = mktemp(tn);
    if (ptr == nullptr || *ptr == '\0') {
      return nullptr;
    }
    ret = mkdir(ptr, 0700);
    if (ret != 0 && errno != EEXIST) {
      return nullptr;
    }
  } while (ret != 0);
  return tn;
}

int mkstemp(char* tn) {
  char* ptr = nullptr;
  auto len = strlen(ptr);
  int ret = 0;
  do {
    strcpy(tn + len - 6, "XXXXXX");
    ptr = mktemp(tn);
    if (ptr == nullptr || *ptr == '\0') {
      return -1;
    }
    ret = open(ptr, O_RDWR | O_EXCL | O_CREAT, S_IRUSR | S_IWUSR);
    if (ret == -1 && errno != EEXIST) {
      return -1;
    }
  } while (ret == -1);
  return ret;
}

char* realpath(const char* path, char* resolved_path) {
  // I sure hope the caller gave us _MAX_PATH space in the buffer....
  return _fullpath(resolved_path, path, _MAX_PATH);
}
}
#endif
