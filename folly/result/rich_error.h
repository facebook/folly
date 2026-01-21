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

#include <folly/Portability.h> // FOLLY_HAS_RESULT
#include <folly/lang/Exception.h>
#include <folly/lang/Pretty.h>
#include <folly/result/detail/rich_error_common.h>
#include <folly/result/rich_error_fwd.h>

#if FOLLY_HAS_RESULT

/// See: docs/rich_error.md

namespace folly {

/// Syntax sugar for `folly::get_exception<rich_error_base>()`.
///
/// This retrieves the **underlying** rich error.  `enriching_errors.md`
/// describes this in detail, but in brief:
///   - Enrichment only works with `folly/result/` error containers.
///   - If the underlying error is not a `rich_error_base`, you will get back
///     null, **even if** it was later enriched.
///   - Furthermore, if you used `enrich_non_value()` or another enriching
///     wrapper, `->partial_message()` will NOT give you the enrichment
///     message, but the rather the one from the base rich error.
///
/// Very rarely, you may want `get_outer_exception` to access the enrichment
/// wrapper object itself.
inline constexpr get_exception_fn<rich_error_base> get_rich_error{};

// See: docs/rich_error.md
template <typename UserBase> // must derive from `rich_error_base`
class [[nodiscard]] rich_error final
    : public detail::rich_error_with_partial_message<UserBase>,
      public std::exception {
 private:
  using Base = detail::rich_error_with_partial_message<UserBase>;

  void only_rich_error_may_instantiate(
      rich_error_base::only_rich_error_may_instantiate_t) override {}

 public:
  const char* what() const noexcept override {
    // These assertions are under `what()` since this virtual member **must** be
    // instantiated together with the class template. Has a manual test.
    detail::static_assert_is_valid_rich_error_type<UserBase>(this);
    auto msg = Base::partial_message();
    return msg[0] ? msg : pretty_name<rich_error>();
  }

  rich_error() = default;

  // Delegate non-standard ctors to the user-defined `Base`
  template <typename T, typename... Ts>
    requires(
        !std::is_same_v<rich_error, std::remove_cvref_t<T>> &&
        !std::is_same_v<detail::immortal_rich_error_private_t, T>)
  explicit rich_error(T&& t, Ts&&... ts)
      : Base{static_cast<T&&>(t), static_cast<Ts&&>(ts)...} {}

  rich_error(detail::immortal_rich_error_private_t, const Base& that)
      : Base{that} {}

  // This isn't `Base` because `immortal_rich_error` will wrap it again.
  using folly_detail_base_of_rich_error = UserBase;
  // NB: This hint is a no-op, unless `UserBase` declares one, in which case
  // this just prevents the use of the wrong hint.
  using folly_get_exception_hint_types = tag_t<rich_error>;
};

} // namespace folly

#endif // FOLLY_HAS_RESULT
