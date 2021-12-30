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

#include <folly/ExceptionString.h>

#include <utility>

#include <folly/Demangle.h>
#include <folly/lang/Exception.h>
#include <folly/lang/TypeInfo.h>

namespace folly {

namespace {

fbstring exception_string_type(std::type_info const* ti) {
  return ti ? demangle(*ti) : "<unknown exception>";
}

} // namespace

/**
 * Debug string for an exception: include type and what(), if
 * defined.
 */
fbstring exceptionStr(std::exception const& e) {
  auto prefix = exception_string_type(folly::type_info_of(e));
  return std::move(prefix) + ": " + e.what();
}

fbstring exceptionStr(std::exception_ptr const& ep) {
  if (auto ex = exception_ptr_get_object<std::exception>(ep)) {
    return exceptionStr(*ex);
  }
  return exception_string_type(exception_ptr_get_type(ep));
}

} // namespace folly
