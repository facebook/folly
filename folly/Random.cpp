/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/Random.h"

#include <atomic>
#include <unistd.h>
#include <sys/time.h>

namespace folly {

namespace {
std::atomic<uint32_t> seedInput(0);
}

uint32_t randomNumberSeed() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const uint32_t kPrime0 = 51551;
  const uint32_t kPrime1 = 61631;
  const uint32_t kPrime2 = 64997;
  const uint32_t kPrime3 = 111857;
  return kPrime0 * (seedInput++)
       + kPrime1 * static_cast<uint32_t>(getpid())
       + kPrime2 * static_cast<uint32_t>(tv.tv_sec)
       + kPrime3 * static_cast<uint32_t>(tv.tv_usec);
}

}
