/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

/***
 *  include or backport:
 *  * std::chrono::ceil
 *  * std::chrono::floor
 *  * std::chrono::round
 */

#if __cpp_lib_chrono >= 201510 || _LIBCPP_STD_VER > 14 || _MSC_VER

namespace folly {
namespace chrono {

/* using override */ using std::chrono::abs;
/* using override */ using std::chrono::ceil;
/* using override */ using std::chrono::floor;
/* using override */ using std::chrono::round;
} // namespace chrono
} // namespace folly

#else

namespace folly {
namespace chrono {

namespace detail {

//  from: http://en.cppreference.com/w/cpp/chrono/duration/ceil, CC-BY-SA
template <typename T>
struct is_duration : std::false_type {};
template <typename Rep, typename Period>
struct is_duration<std::chrono::duration<Rep, Period>> : std::true_type {};

template <typename To, typename Duration>
constexpr To ceil_impl(Duration const& d, To const& t) {
  return t < d ? t + To{1} : t;
}

template <typename To, typename Duration>
constexpr To floor_impl(Duration const& d, To const& t) {
  return t > d ? t - To{1} : t;
}

template <typename To, typename Diff>
constexpr To round_impl(To const& t0, To const& t1, Diff diff0, Diff diff1) {
  return diff0 < diff1 ? t0 : diff1 < diff0 ? t1 : t0.count() & 1 ? t1 : t0;
}

template <typename To, typename Duration>
constexpr To round_impl(Duration const& d, To const& t0, To const& t1) {
  return round_impl(t0, t1, d - t0, t1 - d);
}

template <typename To, typename Duration>
constexpr To round_impl(Duration const& d, To const& t0) {
  return round_impl(d, t0, t0 + To{1});
}
} // namespace detail

//  mimic: std::chrono::abs, C++17
template <
    typename Rep,
    typename Period,
    typename = typename std::enable_if<
        std::chrono::duration<Rep, Period>::min() <
        std::chrono::duration<Rep, Period>::zero()>::type>
constexpr std::chrono::duration<Rep, Period> abs(
    std::chrono::duration<Rep, Period> const& d) {
  return d < std::chrono::duration<Rep, Period>::zero() ? -d : d;
}

//  mimic: std::chrono::ceil, C++17
//  from: http://en.cppreference.com/w/cpp/chrono/duration/ceil, CC-BY-SA
template <
    typename To,
    typename Rep,
    typename Period,
    typename = typename std::enable_if<detail::is_duration<To>::value>::type>
constexpr To ceil(std::chrono::duration<Rep, Period> const& d) {
  return detail::ceil_impl(d, std::chrono::duration_cast<To>(d));
}

//  mimic: std::chrono::ceil, C++17
//  from: http://en.cppreference.com/w/cpp/chrono/time_point/ceil, CC-BY-SA
template <
    typename To,
    typename Clock,
    typename Duration,
    typename = typename std::enable_if<detail::is_duration<To>::value>::type>
constexpr std::chrono::time_point<Clock, To> ceil(
    std::chrono::time_point<Clock, Duration> const& tp) {
  return std::chrono::time_point<Clock, To>{ceil<To>(tp.time_since_epoch())};
}

//  mimic: std::chrono::floor, C++17
//  from: http://en.cppreference.com/w/cpp/chrono/duration/floor, CC-BY-SA
template <
    typename To,
    typename Rep,
    typename Period,
    typename = typename std::enable_if<detail::is_duration<To>::value>::type>
constexpr To floor(std::chrono::duration<Rep, Period> const& d) {
  return detail::floor_impl(d, std::chrono::duration_cast<To>(d));
}

//  mimic: std::chrono::floor, C++17
//  from: http://en.cppreference.com/w/cpp/chrono/time_point/floor, CC-BY-SA
template <
    typename To,
    typename Clock,
    typename Duration,
    typename = typename std::enable_if<detail::is_duration<To>::value>::type>
constexpr std::chrono::time_point<Clock, To> floor(
    std::chrono::time_point<Clock, Duration> const& tp) {
  return std::chrono::time_point<Clock, To>{floor<To>(tp.time_since_epoch())};
}

//  mimic: std::chrono::round, C++17
//  from: http://en.cppreference.com/w/cpp/chrono/duration/round, CC-BY-SA
template <
    typename To,
    typename Rep,
    typename Period,
    typename = typename std::enable_if<
        detail::is_duration<To>::value &&
        !std::chrono::treat_as_floating_point<typename To::rep>::value>::type>
constexpr To round(std::chrono::duration<Rep, Period> const& d) {
  return detail::round_impl(d, floor<To>(d));
}

//  mimic: std::chrono::round, C++17
//  from: http://en.cppreference.com/w/cpp/chrono/time_point/round, CC-BY-SA
template <
    typename To,
    typename Clock,
    typename Duration,
    typename = typename std::enable_if<
        detail::is_duration<To>::value &&
        !std::chrono::treat_as_floating_point<typename To::rep>::value>::type>
constexpr std::chrono::time_point<Clock, To> round(
    std::chrono::time_point<Clock, Duration> const& tp) {
  return std::chrono::time_point<Clock, To>{round<To>(tp.time_since_epoch())};
}
} // namespace chrono
} // namespace folly

#endif

namespace folly {
namespace chrono {

struct coarse_steady_clock {
  using duration = std::chrono::milliseconds;
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
  using duration = std::chrono::milliseconds;
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
