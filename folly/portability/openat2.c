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

#include <folly/folly-config.h>

#if FOLLY_HAVE_OPENAT2

#include <folly/portability/openat2.h>

#include <sys/syscall.h>
#include <unistd.h>

int openat2(int dirfd, const char* pathname, const struct open_how* how) {
  return syscall(SYS_openat2, dirfd, pathname, how, sizeof(struct open_how));
}

#endif
