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

#include <exception>
#include <string>
#include <type_traits>
#include <typeinfo>

#include <folly/Demangle.h>
#include <folly/FBString.h>
#include <folly/Portability.h>

namespace folly {

/**
 * Debug string for an exception: include type and what(), if
 * defined.
 */
fbstring exceptionStr(const std::exception& e);

fbstring exceptionStr(std::exception_ptr ep);

template <typename E>
auto exceptionStr(const E& e) -> typename std::
    enable_if<!std::is_base_of<std::exception, E>::value, fbstring>::type {
#if FOLLY_HAS_RTTI
  return demangle(typeid(e));
#else
  (void)e;
  return "Exception (no RTTI available)";
#endif
}

} // namespace folly
