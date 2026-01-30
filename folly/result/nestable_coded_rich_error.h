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

#include <folly/result/coded_rich_error.h>
#include <folly/result/rich_error.h>
#include <folly/result/rich_error_base.h>
#include <folly/result/rich_exception_ptr.h>
#include <folly/result/rich_msg.h>

#include <folly/Portability.h> // FOLLY_HAS_RESULT

#if FOLLY_HAS_RESULT

namespace folly {

namespace detail {

template <typename Derived, bool InheritCodes, typename... Codes>
class nestable_coded_rich_error_crtp : public coded_rich_error<Codes...> {
 private:
  rich_exception_ptr next_{};

 public:
  // Future: Could add `make()` a la `coded_rich_error::make`, if useful.

  // Avoid. For ergonomics, prefer `make_nestable_coded_rich_error()` to
  // construct `rich_error<nestable_coded_rich_error>{...}`.
  explicit constexpr nestable_coded_rich_error_crtp(
      Codes... codes, rich_exception_ptr next, rich_msg msg)
      : coded_rich_error<Codes...>{codes..., std::move(msg)},
        next_{std::move(next)} {}

  const rich_exception_ptr* next_error_for_epitaph() const noexcept override {
    return (next_ != rich_exception_ptr{}) ? &next_ : nullptr;
  }

  constexpr void retrieve_code(rich_error_code_query& q) const override {
    coded_rich_error<Codes...>::retrieve_code(q);
    if constexpr (InheritCodes) {
      if (!q.has_value()) {
        if (auto ex = folly::get_exception<folly::rich_error_base>(next_)) {
          ex->retrieve_code(q);
        }
      }
    }
  }

  using folly_get_exception_hint_types = rich_error_hints<Derived>;
};

} // namespace detail

/// nestable_coded_rich_error
///
/// Like `coded_rich_error` (read its docs first!), plus the ability to nest
/// around another "next" exception for better debuggability.
///
/// This becomes the new "underlying exception" in `rich_error_base` jargon. The
/// nested error's type, message, location, codes are NOT exposed -- they're
/// only visible via fmt / ostream output for debugging.
///
/// If you do NOT intend to "hide" the type of the old error (`next` arg), then
/// you may be wanting the transparent annotations from `epitaph.h`.
///
/// Future: This currently doesn't support nesting during constant evaluation.
/// That would require adding a consteval ctor taking `rich_exception_ptr*`,
/// similar to `ConstErrWithNext` in `immortal_rich_error_test.cpp`.  Do add
/// this (plus a `constexpr` test) if the need comes up.
template <typename... Codes>
struct nestable_coded_rich_error
    : public detail::nestable_coded_rich_error_crtp<
          nestable_coded_rich_error<Codes...>,
          false,
          Codes...> {
  using detail::nestable_coded_rich_error_crtp<
      nestable_coded_rich_error<Codes...>,
      false,
      Codes...>::nestable_coded_rich_error_crtp;
};

/// inheriting_coded_rich_error
///
/// Behaves almost identically to `nestable_coded_rich_error`, but it IS able to
/// inherits codes from the nested error. For a `get_user_error_code<Code>()`
/// query, if `Code` is in `Codes`, it will be fulfilles by this class.
/// Otherwise, the query will be forwarded to the nested error.
template <typename... Codes>
struct inheriting_coded_rich_error
    : public detail::nestable_coded_rich_error_crtp<
          inheriting_coded_rich_error<Codes...>,
          true,
          Codes...> {
  using detail::nestable_coded_rich_error_crtp<
      inheriting_coded_rich_error<Codes...>,
      true,
      Codes...>::nestable_coded_rich_error_crtp;
};

/// Make `rich_error<nestable_coded_rich_error<Codes...>>` or
/// `rich_error<inheriting_coded_rich_error<Codes...>>`, deducing code types.
///
/// Common case -- single code:
///   make_..._coded_rich_error(MyCode::VAL, next, "info={}", info)
/// Power users -- multiple codes:
///   make_..._coded_rich_error(rich_msg{"msg"}, next, Code1::A, Code2::B)
inline constexpr make_coded_rich_error_fn<
    nestable_coded_rich_error,
    rich_exception_ptr>
    make_nestable_coded_rich_error{};
inline constexpr make_coded_rich_error_fn<
    inheriting_coded_rich_error,
    rich_exception_ptr>
    make_inheriting_coded_rich_error{};

} // namespace folly

#endif // FOLLY_HAS_RESULT
