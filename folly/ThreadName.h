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

#pragma once

#include <pthread.h>
#include "Range.h"

namespace folly {

inline bool setThreadName(pthread_t id, StringPiece name) {
#if (defined(__GLIBC__) && __GLIBC_PREREQ(2, 12))
  return 0 == pthread_setname_np(id, name.fbstr().substr(0, 15).c_str());
#else
  return false;
#endif
}

inline bool setThreadName(StringPiece name) {
  return setThreadName(pthread_self(), name);
}

}
