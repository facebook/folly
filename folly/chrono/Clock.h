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

#include <chrono>

/**
 * Namespace for folly chrono types.
 *
 * Using a separate namespace for clock types to minimize type conflicts in
 * tests and other code which may be using `using namespace folly` while also
 * having aliased chrono types.
 */
namespace folly::chrono {

/**
 * Clock interface.
 *
 * Abstraction enables tests to control the current time.
 */
template <typename ClockType>
class Clock {
 public:
  using TimePoint = typename ClockType::time_point;
  Clock() = default;
  virtual ~Clock() = default;

  /**
   * Returns current time.
   */
  [[nodiscard]] virtual TimePoint now() const = 0;
};

/**
 * Implementation of ClockInterface for given std::chrono ClockType.
 */
template <typename ClockType>
class ClockImpl : public Clock<ClockType> {
 public:
  using TimePoint = typename ClockType::time_point;
  ClockImpl() = default;
  ~ClockImpl() override = default;
  [[nodiscard]] TimePoint now() const override { return ClockType::now(); }
};

using SteadyClock = Clock<std::chrono::steady_clock>;
using SteadyClockImpl = ClockImpl<std::chrono::steady_clock>;
using SystemClock = Clock<std::chrono::system_clock>;
using SystemClockImpl = ClockImpl<std::chrono::system_clock>;

} // namespace folly::chrono
