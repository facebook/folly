/*
 * Copyright 2013 Facebook, Inc.
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

// Some utilities used by AtomicHashArray and AtomicHashMap
//
// Note: no include guard; different -inl.h files include this and
// undef it more than once in a translation unit.

#if !(defined(__x86__) || defined(__i386__) || defined(__x86_64__))
#define FOLLY_SPIN_WAIT(condition)                \
   for (int counter = 0; condition; ++counter) {  \
     if (counter < 10000) continue;               \
     pthread_yield();                             \
   }
#else
#define FOLLY_SPIN_WAIT(condition)              \
  for (int counter = 0; condition; ++counter) { \
    if (counter < 10000) {                      \
      asm volatile("pause");                    \
      continue;                                 \
    }                                           \
    pthread_yield();                            \
  }
#endif
