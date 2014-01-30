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

#include <folly/detail/Futex.h>

namespace folly { namespace detail {

/* see Futex.h */
FutexResult futexErrnoToFutexResult(int returnVal, int futexErrno) {
  if (returnVal == 0) {
    return FutexResult::AWOKEN;
  }
  switch(futexErrno) {
    case ETIMEDOUT:
      return FutexResult::TIMEDOUT;
    case EINTR:
      return FutexResult::INTERRUPTED;
    case EWOULDBLOCK:
      return FutexResult::VALUE_CHANGED;
    default:
      assert(false);
      /* Shouldn't reach here. Just return one of the FutexResults */
      return FutexResult::VALUE_CHANGED;
  }
}

}}
