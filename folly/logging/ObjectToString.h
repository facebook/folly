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

#include <folly/Conv.h>
#include <folly/Portability.h>
#include <folly/lang/Exception.h>
#include <folly/lang/TypeInfo.h>

/*
 * This file contains functions for converting arbitrary objects to strings for
 * logging purposes.
 *
 * - folly::logging::objectToString(object) --> std::string
 * - folly::logging::appendToString(result, object)
 * - folly::logging::appendToString(result, object1, object2, ...)
 *
 * These functions behave somewhat similarly to `folly::to<std::string>(object)`
 * but always produce a result, rather than failing to compile for objects that
 * do not have a predefined mechanism for converting them to a string.
 *
 * If a `toAppend(std::string&, const Arg& object)` function has been defined
 * for the given object type this will be used to format the object.  (This is
 * the same mechanism used by `folly::to<std::string>()`.)  If a `toAppend()`
 * function is not defined these functions fall back to emitting a string that
 * includes the object type name, the object size, and a hex-dump representation
 * of the object contents.
 *
 * When invoked with multiple arguments `appendToString()` puts ", " between
 * each argument in the output.  This also differs from
 * `folly::to<std::string>()`, which puts no extra output between arguments.
 */

namespace folly {
namespace logging {

namespace detail {
void appendRawObjectInfo(
    std::string& result,
    const std::type_info* type,
    const uint8_t* data,
    size_t length);
} // namespace detail

/**
 * Append raw information about an object to a string.
 *
 * This is used as a fallback for objects that we do not otherwise know how to
 * print.  This emits:
 * - The object type name (if RTTI is supported)
 * - The object size
 * - A hexdump of the object contents.
 *
 * e.g.
 *   [MyStruct of size 4: 37 6f af 2a]
 *   [AnotherClass of size 8: f9 48 78 85 56 54 8f 54]
 */
template <typename Arg>
inline void appendRawObjectInfo(std::string& str, const Arg* arg) {
  detail::appendRawObjectInfo(
      str,
      FOLLY_TYPE_INFO_OF(*arg),
      reinterpret_cast<const uint8_t*>(arg),
      sizeof(*arg));
}

/*
 * Helper functions for object to string conversion.
 * These are in a detail namespace so that we can include a using directive in
 * order to do proper argument-dependent lookup of the correct toAppend()
 * function to use.
 */
namespace detail {
/* using override */
using folly::toAppend;

template <typename Arg>
auto appendObjectToString(std::string& str, const Arg* arg, int) -> decltype(
    toAppend(std::declval<Arg>(), std::declval<std::string*>()),
    std::declval<void>()) {
  ::folly::catch_exception<const std::exception&>(
      [&] { toAppend(*arg, &str); },
      [&](const std::exception&) {
        // If anything goes wrong in `toAppend()` fall back to
        // appendRawObjectInfo()
        ::folly::logging::appendRawObjectInfo(str, arg);
      });
}

template <typename Arg>
inline void appendObjectToString(std::string& str, const Arg* arg, long) {
  ::folly::logging::appendRawObjectInfo(str, arg);
}
} // namespace detail

/**
 * Convert an arbitrary object to a string for logging purposes.
 */
template <typename Arg>
std::string objectToString(const Arg& arg) {
  std::string result;
  ::folly::logging::detail::appendObjectToString(result, &arg, 0);
  return result;
}

/**
 * Append an arbitrary object to a string for logging purposes.
 */
template <typename Arg>
void appendToString(std::string& result, const Arg& arg) {
  ::folly::logging::detail::appendObjectToString(result, &arg, 0);
}

/**
 * Append an arbitrary group of objects to a string for logging purposes.
 *
 * This function outputs a comma and space (", ") between each pair of objects.
 */
template <typename Arg1, typename... Args>
void appendToString(
    std::string& result, const Arg1& arg1, const Args&... remainder) {
  ::folly::logging::detail::appendObjectToString(result, &arg1, 0);
  result.append(", ");
  ::folly::logging::appendToString(result, remainder...);
}

} // namespace logging
} // namespace folly
