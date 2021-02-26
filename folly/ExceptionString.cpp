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

#include <folly/ExceptionString.h>

#include <utility>

#include <folly/CPortability.h>
#include <folly/CppAttributes.h>
#include <folly/Demangle.h>
#include <folly/functional/Invoke.h>
#include <folly/lang/Exception.h>

namespace folly {

/**
 * Debug string for an exception: include type and what(), if
 * defined.
 */
fbstring exceptionStr(const std::exception& e) {
#if FOLLY_HAS_RTTI
  fbstring rv(demangle(typeid(e)));
  rv += ": ";
#else
  fbstring rv("Exception (no RTTI available): ");
#endif
  rv += e.what();
  return rv;
}

namespace {

FOLLY_CREATE_MEMBER_INVOKER(invoke_cxa_exception_type_fn, __cxa_exception_type);

struct fallback_cxa_exception_type_fn {
  FOLLY_MAYBE_UNUSED FOLLY_ERASE_HACK_GCC std::type_info const* operator()(
      std::exception_ptr const&) const noexcept {
    return nullptr;
  }
};

using invoke_or_fallback_cxa_exception_type_fn = std::conditional_t<
    is_invocable_r_v<
        std::type_info const*,
        invoke_cxa_exception_type_fn,
        std::exception_ptr const&>,
    invoke_cxa_exception_type_fn,
    fallback_cxa_exception_type_fn>;

FOLLY_INLINE_VARIABLE constexpr invoke_or_fallback_cxa_exception_type_fn
    invoke_or_fallback_cxa_exception_type;

} // namespace

fbstring exceptionStr(std::exception_ptr ep) {
  if (!kHasExceptions) {
    return "Exception (catch unavailable)";
  }
  auto type = invoke_or_fallback_cxa_exception_type(ep);
  return catch_exception(
      [&]() -> fbstring {
        return catch_exception<std::exception const&>(
            [&]() -> fbstring { std::rethrow_exception(std::move(ep)); },
            [](auto&& e) { return exceptionStr(e); });
      },
      [&]() -> fbstring {
        return type ? demangle(*type) : "<unknown exception>";
      });
}

} // namespace folly
