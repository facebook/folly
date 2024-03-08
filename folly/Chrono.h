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
#include <ctime>
#include <stdexcept>
#include <type_traits>

#include <folly/Portability.h>
#include <folly/lang/Exception.h>
#include <folly/portability/Time.h>

namespace folly {
namespace chrono {

/* using override */ using std::chrono::abs;
/* using override */ using std::chrono::ceil;
/* using override */ using std::chrono::floor;
/* using override */ using std::chrono::round;

//  steady_clock_spec
//
//  All clocks with this spec share epoch and tick rate.
struct steady_clock_spec {};

//  system_clock_spec
//
//  All clocks with this spec share epoch and tick rate.
struct system_clock_spec {};

//  clock_traits
//
//  Detects and reexports per-clock traits.
//
//  Specializeable for clocks for which trait detection fails..
template <typename Clock>
struct clock_traits {
 private:
  template <typename C>
  using detect_spec_ = typename C::folly_spec;

 public:
  using spec = detected_or_t<void, detect_spec_, Clock>;
};

template <>
struct clock_traits<std::chrono::steady_clock> {
  using spec = steady_clock_spec;
};
template <>
struct clock_traits<std::chrono::system_clock> {
  using spec = system_clock_spec;
};

struct coarse_steady_clock {
  using folly_spec = steady_clock_spec;

  using duration = std::chrono::steady_clock::duration;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<coarse_steady_clock>;
  constexpr static bool is_steady = true;

  static time_point now() noexcept {
#ifndef CLOCK_MONOTONIC_COARSE
    auto time = std::chrono::steady_clock::now().time_since_epoch();
#else
    timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
    if (kIsDebug && (ret != 0)) {
      throw_exception<std::runtime_error>(
          "Error using CLOCK_MONOTONIC_COARSE.");
    }
    auto time =
        std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
#endif
    return time_point(std::chrono::duration_cast<duration>(time));
  }
};

struct coarse_system_clock {
  using folly_spec = system_clock_spec;

  using duration = std::chrono::system_clock::duration;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<coarse_system_clock>;
  constexpr static bool is_steady = false;

  static time_point now() noexcept {
#ifndef CLOCK_REALTIME_COARSE
    auto time = std::chrono::system_clock::now().time_since_epoch();
#else
    timespec ts;
    int ret = clock_gettime(CLOCK_REALTIME_COARSE, &ts);
    if (kIsDebug && (ret != 0)) {
      throw_exception<std::runtime_error>("Error using CLOCK_REALTIME_COARSE.");
    }
    auto time =
        std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
#endif
    return time_point(std::chrono::duration_cast<duration>(time));
  }

  static std::time_t to_time_t(const time_point& t) noexcept {
    auto d = t.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(d).count();
  }

  static time_point from_time_t(std::time_t t) noexcept {
    return time_point(
        std::chrono::duration_cast<duration>(std::chrono::seconds(t)));
  }
};

} // namespace chrono
} // namespace folly
