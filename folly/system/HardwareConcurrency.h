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

#pragma once

#include <utility>

namespace folly {

/// available_concurrency
///
/// Returns a number suitable for use as a size for thread-pools or as a size
/// for cpu-striped data-structures. Consider the purpose of this function as
/// returning some measure of the concurrency available to the calling program.
///
/// Caution: May fail by returning 0, which is never a success value.
///
/// Caution: The value returned may change over the lifetime of the process or
/// of the system.
///
/// Caution: This does not necessarily return a number of processors. This is
/// *not* an alias, for example, for get_nprocs, get_nprocs_conf, or sysconf
/// with key any of _SC_NPROCESSORS_ONLN or _SC_NPROCESSORS_CONF.
///
/// Caution: May succeed but return a number that is not necessarily larger than
/// the maximum value returnable by a call to getcpu(). For cpu-striped data
/// structures, this means that the number returned by getcpu() may not be a
/// valid index into the data-structure; either the result must be %'d by the
/// number of stripes, or the number of stripes must be the bit-ceiling of the
/// result of this function and then the result of getcpu() must be &'d. For
/// use-cases which must be free of % and & operations, this function is not
/// suitable.
///
/// Note: This may use arbitrary sources of information (signals) as inputs for
/// calculating a suitable return value. It is not specified to use any specific
/// signal in any specific way.
/// * The result of std::thread::hardware_concurrency (all).
/// * The result of get_nprocs, get_nprocs_conf, or sysconf as noted above.
/// * The result of sched_getaffinity (linux but not android).
/// * Unspecified others....
///
/// mimic: std::thread::hardware_concurrency
unsigned int available_concurrency() noexcept;

/// hardware_concurrency
///
/// An alias for available_concurrency.
inline unsigned int hardware_concurrency() noexcept {
  return available_concurrency();
}

} // namespace folly
