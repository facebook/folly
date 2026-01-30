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

#include <folly/result/coro.h>
#include <folly/result/epitaph.h>

#if FOLLY_HAS_RESULT

namespace folly {

/// or_unwind_epitaph
///
/// Syntax sugar for `or_unwind(epitaph(...)`.
///
/// On the value path: epitaphs are skipped (just like `epitaph`).
/// On the error-or-stopped path: epitaphs are added, then the error is
/// propagated.
template <typename T, typename... Args>
auto or_unwind_epitaph(
    result<T> r,
    ext::format_string_and_location<std::type_identity_t<Args>...> snl = "",
    Args const&... args) {
  return or_unwind_owning(epitaph(std::move(r), snl, args...));
}
template <typename... Args>
auto or_unwind_epitaph(
    error_or_stopped eos,
    ext::format_string_and_location<std::type_identity_t<Args>...> snl = "",
    Args const&... args) {
  return or_unwind_owning(epitaph(std::move(eos), snl, args...));
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
