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

// Wrapper around <chrono> that hides away some gcc 4.6 issues
#ifndef FOLLY_CHRONO_H_
#define FOLLY_CHRONO_H_

#include <chrono>
#include "folly/Portability.h"

// gcc 4.6 uses an obsolete name for steady_clock, although the implementation
// is the same
#if __GNUC_PREREQ(4, 6) && !__GNUC_PREREQ(4, 7)
namespace std { namespace chrono {
typedef monotonic_clock steady_clock;
}}  // namespaces
#endif

#endif /* FOLLY_CHRONO_H_ */

