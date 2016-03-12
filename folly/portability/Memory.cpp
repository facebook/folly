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

#include <folly/portability/Memory.h>

#include <cerrno>
#include <cstdlib>

#ifdef __ANDROID__
#include <android/api-level.h>
#endif

namespace folly {
namespace detail {

#ifdef _WIN32
void* aligned_malloc(size_t size, size_t align) { return nullptr; }

void aligned_free(void* aligned_ptr) {}
#elif defined(__ANDROID__) && (__ANDROID_API__ <= 15)

void* aligned_malloc(size_t size, size_t align) { return memalign(align, size) }

void aligned_free(void* aligned_ptr) { free(aligned_ptr); }

#else
// Use poxis_memalign, but mimic the behavior of memalign
void* aligned_malloc(size_t size, size_t align) {
  void* ptr = nullptr;
  int rc = posix_memalign(&ptr, align, size);
  if (rc == 0) {
    return ptr;
  }
  errno = rc;
  return nullptr;
}

void aligned_free(void* aligned_ptr) { free(aligned_ptr); }

#endif
}
}
