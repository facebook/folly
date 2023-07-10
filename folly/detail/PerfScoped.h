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

#include <memory>
#include <string>
#include <vector>

namespace folly {
namespace detail {

#if defined(__linux__) && !defined(__ANDROID__)
#define FOLLY_PERF_IS_SUPPORTED 1
#else
#define FOLLY_PERF_IS_SUPPORTED 0
#endif

/*
 * A folly::benchmark helper for attaching `perf` profiler
 * to a given block of code.
 *
 * Only available on linux.
 */
class PerfScoped {
 public:
  // Not running. Used to be able to move things/not start
  PerfScoped();

  // Actually starts perf
  // Output is for testing, if passed, perf output will be
  // put there.
  //
  // NOTE: noretrun has to be here to ignore a warning
#if !FOLLY_PERF_IS_SUPPORTED
  [[noreturn]]
#endif
  explicit PerfScoped(
      const std::vector<std::string>& args, std::string* output = nullptr);

  // Sends Ctrl-C to stop recording.
  ~PerfScoped() noexcept;

  PerfScoped(const PerfScoped&) = delete;
  PerfScoped& operator=(const PerfScoped&) = delete;

  PerfScoped(PerfScoped&& x) noexcept;
  PerfScoped& operator=(PerfScoped&& x) noexcept;

 private:
  class PerfScopedImpl;

  std::unique_ptr<PerfScopedImpl> pimpl_;
};

} // namespace detail
} // namespace folly
