/*
 * Copyright 2014 Facebook, Inc.
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

#include "folly/Malloc.h"

namespace folly {

// How do we determine that we're using jemalloc?
// In the hackiest way possible. We allocate memory using malloc() and see if
// the per-thread counter of allocated memory increases. This makes me feel
// dirty inside. Also note that this requires jemalloc to have been compiled
// with --enable-stats.
bool usingJEMallocSlow() {
  // Some platforms (*cough* OSX *cough*) require weak symbol checks to be
  // in the form if (mallctl != NULL). Not if (mallctl) or if (!mallctl) (!!).
  // http://goo.gl/xpmctm
  if (allocm == nullptr || rallocm == nullptr || mallctl == nullptr) {
    return false;
  }

  // "volatile" because gcc optimizes out the reads from *counter, because
  // it "knows" malloc doesn't modify global state...
  volatile uint64_t* counter;
  size_t counterLen = sizeof(uint64_t*);

  if (mallctl("thread.allocatedp", static_cast<void*>(&counter), &counterLen,
              nullptr, 0) != 0) {
    return false;
  }

  if (counterLen != sizeof(uint64_t*)) {
    return false;
  }

  uint64_t origAllocated = *counter;

  void* ptr = malloc(1);
  if (!ptr) {
    // wtf, failing to allocate 1 byte
    return false;
  }
  free(ptr);

  return (origAllocated != *counter);
}

}  // namespaces

