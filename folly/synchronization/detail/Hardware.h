/*
 * Copyright 2019-present Facebook, Inc.
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

#include <folly/Portability.h>

namespace folly {
namespace hardware {

// Valid status values returned from rtmBegin.
// kRtmDisabled is a new return value indicating that RTM support is unavailable
// on the platform or the compiler.
constexpr unsigned kRtmDisabled = static_cast<unsigned>(-2);
constexpr unsigned kRtmBeginStarted = static_cast<unsigned>(-1);

// Valid abort status bits (when the status value is not kRtmBeginStarted or
// kRtmDisabled), defined as per the Intel RTM specifications:
// https://en.wikipedia.org/wiki/Transactional_Synchronization_Extensions.
constexpr unsigned kRtmAbortExplicit = 1;
constexpr unsigned kRtmAbortRetry = 2;
constexpr unsigned kRtmAbortConflict = 4;
constexpr unsigned kRtmAbortCapacity = 8;
constexpr unsigned kRtmAbortDebug = 16;
constexpr unsigned kRtmAbortNested = 32;

// False if there is no need for a dynamic check to see if
// the current environment supports RTM
constexpr bool kRtmSupportEnabled = kIsArchAmd64;

// Check on cpu support for tsx-rtm
extern bool rtmEnabled();

// Use func ptrs to access the txn functions to avoid txn aborts
// due to plt mapping.
extern unsigned (*const rtmBegin)();
extern void (*const rtmEnd)();
extern bool (*const rtmTest)();

// The abort status code must be known at compile time, so
// the abstraction layer only supports a subset of the full
// range.  rtmAbort(s) fails if s > 4 in the current implementation.
extern void (*const rtmAbort)(unsigned status);

inline unsigned rtmStatusToAbortCode(unsigned status) {
  return status >> 24;
}

} // namespace hardware
} // namespace folly
