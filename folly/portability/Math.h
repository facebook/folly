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

#include <cmath>

namespace folly {

#if !defined(__UCLIBC__)

/**
 * Most platforms hopefully provide std::nextafter, std::remainder.
 */

/* using override */ using std::nextafter;
/* using override */ using std::remainder;

#else // !__UCLIBC__

/**
 * On uclibc, std::nextafter isn't implemented.  However, the C
 * functions and compiler builtins are still provided. Using the GCC
 * builtin is actually slightly faster, as they're constexpr and the
 * use cases within folly are in constexpr context.
 */

constexpr float nextafter(float x, float y) {
  return __builtin_nextafterf(x, y);
}

constexpr double nextafter(double x, double y) {
  return __builtin_nextafter(x, y);
}

constexpr long double nextafter(long double x, long double y) {
  return __builtin_nextafterl(x, y);
}

/**
 * On uclibc, std::remainder isn't implemented.
 * Implement it using builtin versions
 */
constexpr float remainder(float x, float y) {
  return __builtin_remainderf(x, y);
}

constexpr double remainder(double x, double y) {
  return __builtin_remainder(x, y);
}

constexpr long double remainder(long double x, long double y) {
  return __builtin_remainderl(x, y);
}

#endif // !__UCLIBC__
} // namespace folly
