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

#include <folly/portability/Environment.h>

#ifdef _WIN32
#include <Windows.h>

extern "C" {

int setenv(const char* name, const char* value, int overwrite) {
  if (overwrite == 0 && getenv(name) != nullptr) {
    return 0;
  }

  // _putenv_s deletes entries if the value is an empty string,
  // so we have to call the windows API function to safely assign
  // these.
  if (SetEnvironmentVariableA(name, value) != 0) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

int unsetenv(const char* name) {
  if (_putenv_s(name, "") != 0) {
    return -1;
  }
  return 0;
}
}
#endif
