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

#include <folly/memory/Malloc.h>

namespace folly {

namespace detail {

bool usingJEMallocSlow() noexcept {
  // Checking for rallocx != nullptr is not sufficient; we may be in a
  // dlopen()ed module that depends on libjemalloc, so rallocx is resolved, but
  // the main program might be using a different memory allocator.
  // How do we determine that we're using jemalloc? In the hackiest
  // way possible. We allocate memory using malloc() and see if the
  // per-thread counter of allocated memory increases. This makes me
  // feel dirty inside. Also note that this requires jemalloc to have
  // been compiled with --enable-stats.

  // Some platforms (*cough* OSX *cough*) require weak symbol checks to be
  // in the form if (mallctl != nullptr). Not if (mallctl) or if (!mallctl)
  // (!!). http://goo.gl/xpmctm
  if (mallocx == nullptr || rallocx == nullptr || xallocx == nullptr ||
      sallocx == nullptr || dallocx == nullptr || sdallocx == nullptr ||
      nallocx == nullptr || mallctl == nullptr || mallctlnametomib == nullptr ||
      mallctlbymib == nullptr) {
    return false;
  }

  // "volatile" because gcc optimizes out the reads from *counter, because
  // it "knows" malloc doesn't modify global state...
  /* nolint */ volatile uint64_t* counter;
  size_t counterLen = sizeof(uint64_t*);

  if (mallctl(
          "thread.allocatedp",
          static_cast<void*>(&counter),
          &counterLen,
          nullptr,
          0) != 0) {
    return false;
  }

  if (counterLen != sizeof(uint64_t*)) {
    return false;
  }

  uint64_t origAllocated = *counter;

  static void* volatile ptr = malloc(1);
  if (!ptr) {
    // wtf, failing to allocate 1 byte
    return false;
  }

  free(ptr);

  return (origAllocated != *counter);
}

bool usingTCMallocSlow() noexcept {
  // See comment in usingJEMallocSlow().
  if (MallocExtension_Internal_GetNumericProperty == nullptr ||
      sdallocx == nullptr || nallocx == nullptr) {
    return false;
  }
  static const char kAllocBytes[] = "generic.current_allocated_bytes";

  size_t before_bytes = 0;
  getTCMallocNumericProperty(kAllocBytes, &before_bytes);

  static void* volatile ptr = malloc(1);
  if (!ptr) {
    // wtf, failing to allocate 1 byte
    return false;
  }

  size_t after_bytes = 0;
  getTCMallocNumericProperty(kAllocBytes, &after_bytes);

  free(ptr);

  return (before_bytes != after_bytes);
}

} // namespace detail

} // namespace folly
