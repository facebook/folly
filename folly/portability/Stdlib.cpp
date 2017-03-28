/*
 * Copyright 2017 Facebook, Inc.
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
  auto len = strlen(tn);
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
  auto len = strlen(tn);
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

int setenv(const char* name, const char* value, int overwrite) {
  if (overwrite == 0 && getenv(name) != nullptr) {
    return 0;
  }

  if (*value != '\0') {
    auto e = _putenv_s(name, value);
    if (e != 0) {
      errno = e;
      return -1;
    }
    return 0;
  }

  // We are trying to set the value to an empty string, but
  // _putenv_s deletes entries if the value is an empty string,
  // and just calling SetEnvironmentVariableA doesn't update
  // _environ, so we have to do these terrible things.
  if (_putenv_s(name, "  ") != 0) {
    errno = EINVAL;
    return -1;
  }

  // Here lies the documentation we blatently ignore to make
  // this work >_>...
  *getenv(name) = '\0';
  // This would result in a double null termination, which
  // normally signifies the end of the environment variable
  // list, so we stick a completely empty environment variable
  // into the list instead.
  *(getenv(name) + 1) = '=';

  // If _wenviron is null, the wide environment has not been initialized
  // yet, and we don't need to try to update it.
  // We have to do this otherwise we'd be forcing the initialization and
  // maintenance of the wide environment even though it's never actually
  // used in most programs.
  if (_wenviron != nullptr) {
    wchar_t buf[_MAX_ENV + 1];
    size_t len;
    if (mbstowcs_s(&len, buf, _MAX_ENV + 1, name, _MAX_ENV) != 0) {
      errno = EINVAL;
      return -1;
    }
    *_wgetenv(buf) = u'\0';
    *(_wgetenv(buf) + 1) = u'=';
  }

  // And now, we have to update the outer environment to have
  // a proper empty value.
  if (!SetEnvironmentVariableA(name, value)) {
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

#if defined(_WIN32) || defined (__CYGWIN__) || defined(__APPLE__)
#include <string>
extern char **environ;
extern "C" {
int clearenv(void) {
  char **env = environ;
  while (env[0] != nullptr) {
    size_t len = 0;
    while (env[0][len] != '=') ++len;
    ++len;
    {
      try {
        std::string name(env[0], len);
        name[len - 1] = '\0';
        int result = unsetenv(name.c_str());
        if (result != 0) {
          return result;
        }
      } catch (const std::bad_alloc&) {
        return -1;
      }
    }
  }
  return 0;
}
}
#endif