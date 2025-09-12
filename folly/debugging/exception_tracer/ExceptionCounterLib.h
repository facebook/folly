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

#include <ostream>
#include <vector>

#include <folly/debugging/exception_tracer/Compatibility.h>
#include <folly/debugging/exception_tracer/ExceptionTracer.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

#if FOLLY_HAS_EXCEPTION_TRACER

namespace folly {
namespace exception_tracer {

struct ExceptionStats {
  uint64_t count;
  ExceptionInfo info;
};

/**
 * Throw handler that intercepts exception throwing and accumulates stats.
 * Use with registerCxaThrowCallback/unregisterCxaThrowCallback.
 */
void exceptionStatsThrowHandler(
    void*, std::type_info* exType, void (**)(void*)) noexcept;

/**
 * This function accumulates exception throwing statistics across all threads.
 * Please note, that during call to this function, other threads might block
 * on exception throws, so it should be called seldomly.
 * All pef-thread statistics is being reset by the call.
 */
std::vector<ExceptionStats> getExceptionStatistics();

std::ostream& operator<<(std::ostream& out, const ExceptionStats& stats);

} // namespace exception_tracer
} // namespace folly

#endif //  FOLLY_HAS_EXCEPTION_TRACER

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
