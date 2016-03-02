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

#include <errno.h>

#if defined(__ANDROID__)

#include <android/api-level.h>

#if (__ANDROID_API__ <= 15)

int posix_memalign(void** memptr, size_t alignment, size_t size) {
  int rc = 0;
  int saved_errno = errno;
  void* ptr = nullptr;
  ptr = memalign(alignment, size);
  if (nullptr == ptr) {
    rc = errno;
  } else {
    *memptr = ptr;
  }
  errno = saved_errno;
  return rc;
}

#endif
#endif
