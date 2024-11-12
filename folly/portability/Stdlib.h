/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdlib>

#include <folly/CPortability.h>
#include <folly/portability/Config.h>

#if defined(__APPLE__)
#if __has_include(<crt_externs.h>)
#include <crt_externs.h> // @manual
#endif
#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#endif

extern "C" {
#ifdef _WIN32
// These are technically supposed to be defined linux/limits.h and
// sys/param.h respectively, but Windows defines _MAX_PATH in stdlib.h,
// so, instead of creating two headers for a single define each, we put
// them here, where they are likely to already have been included in the
// code that needs them.
#define PATH_MAX _MAX_PATH
#define MAXPATHLEN _MAX_PATH
#define NAME_MAX _MAX_FNAME
#define HOST_NAME_MAX 255

#elif defined(__APPLE__)
// environ doesn't work well with dylibs, so use _NSGetEnviron instead.
#if !__has_include(<crt_externs.h>)
char*** _NSGetEnviron(void);
#endif
#define environ (*_NSGetEnviron())
#endif

#if defined(__FreeBSD__)
// Needed to resolve linkage
extern char** environ;
#endif
}

namespace folly {
namespace portability {
namespace stdlib {
#if !__linux__ && !__FreeBSD__ && !FOLLY_MOBILE
int clearenv();
#endif

#ifdef _WIN32
char* mktemp(char* tn);
char* mkdtemp(char* tn);
int mkstemp(char* tn);
char* realpath(const char* path, char* resolved_path);
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);
#endif
} // namespace stdlib
} // namespace portability
} // namespace folly

#if defined(_WIN32) || (!__linux__ && !FOLLY_MOBILE)
FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wheader-hygiene")
/* using override */ using namespace folly::portability::stdlib;
FOLLY_POP_WARNING
#endif
