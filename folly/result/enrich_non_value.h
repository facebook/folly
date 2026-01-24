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
#include <folly/lang/SafeAssert.h>
#include <folly/result/result.h>
#include <folly/result/rich_error.h>
#include <folly/result/rich_exception_ptr.h>
#include <folly/result/rich_msg.h>

#if FOLLY_HAS_RESULT

namespace folly {

namespace detail {

// `detail::enriched_non_value` is the error type that wraps user errors to
// capture their provenance.
//
// NOTE: We don't forward any `rich_error_base` APIs to the underlying error,
// since this would cause weird duplication in the formatted output, and ...
// in any case, the user can only observe the wrapping error instance via the
// "low-visibility" API of `get_outer_exception`.
//
// IMPORTANT: We should NEVER support adding codes via "transparent" enrichment.
//
// The rationale is the same as why we work hard to expose only the underlying
// error's dynamic type (e.g. `get_rich_error` returns `nullptr` when given an
// enrichment wrapper around a non-rich error).
//
// Specifically, codes often direct program control flow, whereas enrichments
// are inherently discardable.  There is simply no way for us to guarantee that
// codes added via the "enrichment" mechanism would not be accidentally
// discarded, be it by throwing, by using `std::exception_ptr`, or by
// deliberately accessing the underlying error.
class enriched_non_value : public rich_error_base {
 private:
  rich_exception_ptr next_;
  rich_msg msg_;

  void set_underlying_error_on_copy_from(const enriched_non_value& other) {
    FOLLY_SAFE_DCHECK(other.underlying_error());
    if (other.underlying_error() == &other.next_) {
      set_underlying_error(underlying_error_private_t{}, &next_);
    } else if (auto* next_rex = next_.get_outer_exception<rich_error_base>()) {
      // If "underlying" doesn't point at `next_`, it points at something
      // **owned** by `next_`, which means an address can only be safely taken
      // after we copy / move it in.
      //
      // It is tempting to try to micro-optimize this by making "copy" act as
      // "move" for this class, but that would break badly if someone threw an
      // enriched error -- copies during throw are quite common.  Less
      // crucially, we would have to assume that "errors owned by `next_`" are
      // address-stable, though today this is indeed guaranteed.
      //
      // NB: This has to use `mutable_...` to let `rich_exception_ptr` provide
      // `get_mutable_exception`, but per the doc on that `rich_error_base`
      // API, we must be careful not to mutate the underlying REP object.
      underlying_error_private_t priv;
      set_underlying_error(priv, next_rex->mutable_underlying_error(priv));
    }
  }

 public:
  ~enriched_non_value() override = default;
  // IMPORTANT: Custom ctors are required because `underlying_ptr_` points into
  // `this` or data-owned-by-`this`, and thus must be relocated each time.
  enriched_non_value(const enriched_non_value& other)
      : next_{other.next_}, msg_{other.msg_} {
    set_underlying_error_on_copy_from(other);
  }
  enriched_non_value(enriched_non_value&& other) noexcept
      : next_{std::move(other.next_)}, msg_{std::move(other.msg_)} {
    set_underlying_error_on_copy_from(other);
  }
  // These could be implemented (carefully!), but weren't yet needed.
  enriched_non_value& operator=(const enriched_non_value&) = delete;
  enriched_non_value& operator=(enriched_non_value&&) = delete;

  // Construct only via `enrich_non_value()`!
  explicit enriched_non_value(rich_exception_ptr&& next, rich_msg msg)
      : next_{std::move(next)}, msg_{std::move(msg)} {
    // This class should work with an empty `next_`, but it suggests user
    // error -- normal `result` usage never creates empty exception pointers.
    FOLLY_SAFE_DCHECK(next_ != rich_exception_ptr{});
    // Here, we store a pointer into `this`, or something owned-by-`this`.
    // This is only safe because of the special copy/move ctors above.
    if (auto* rex = next_.get_outer_exception<rich_error_base>()) {
      underlying_error_private_t priv;
      // Same note re `mutable_...` as in `set_underlying_error_on_copy_from`.
      if (auto* underlying = rex->mutable_underlying_error(priv)) {
        set_underlying_error(priv, underlying);
        return;
      }
    }
    set_underlying_error(underlying_error_private_t{}, &next_);
  }

  folly::source_location source_location() const noexcept override;
  const char* partial_message() const noexcept override;

  const rich_exception_ptr* next_error_for_enriched_message()
      const noexcept override;

  using folly_get_exception_hint_types = rich_error_hints<enriched_non_value>;
};

} // namespace detail

/// enrich_non_value
///
/// You can enrich errors in `result` & `error_or_stopped` with messages (as
/// allocation-free literals, or via `fmt::format`), and source locations:
///
///   r = enrich_non_value(my_result()) // only the source location
///   r = enrich_non_value(my_result(), "ctx") // location & string literal
///   r = enrich_non_value(my_result(), "fmt {}", a) // formatted, on heap
///
/// This takes ownership of the 1st argument, and returns a same-type value.
///
/// Thread-safety: The underlying exception MAY BE MUTATED, if it derives from
/// `rich_error_base`.  Do NOT allow concurrent access to exception objects!
///
/// If the input is in a value state, it is not changed.  Non-value states (both
/// error & stopped) are enriched with the current location & message.
///
/// Crucially, `enrich_non_value` never changes the type nor the
/// `get_rich_error_code()`s of the error -- access to both will work the same
/// as before enrichment.  So, this works, as do `has_stopped()` checks:
///
///   eos = enrich_non_value(error_or_stopped{std::logic_error{"BUG"}}, "AT");
///   if (auto ex = get_exception<std::logic_error>()) { // NOT `auto*`!
///     LOG(INFO) << ex; // Prints: AT @ src.cpp:42 -> BUG
///   }
///
/// For a wrapper that can change codes, check out `nestable_coded_rich_error`.
///
/// How enrichment works under the hood:
///
///   - The inner `logic_error` is wrapped with a `rich_error` , but this is
///     not observable via APIs besides `get_outer_exception()`.  Seek `result`
///     maintainer advice before using this function!
///
///   - For normal error access, our `get_exception` implementation returns a
///     special `rich_ptr_to_underlying_error` that quacks like a pointer to
///     the underlying error, but prints the enrichment chain when formatted.
///
/// On the "value" path, the perf cost is minimal -- 1 branch.  On the "error"
/// path, adding an enrichment **may** allocate a new `std::exception_ptr` (now
/// 60ns for ctor + dtor, could use some micro-optimization), but given demand
/// we can amortize this to 5ns per call.
///
/// Enrichment works regardless of the underlying exception type. Enrichment
/// data are exposed in an RTTI-free way via `rich_error_base`, so non-rich
/// errors are necessarily wrapped.
///
/// Future: `enriching_errors.md` has pointers on how the enrichment support
/// should evolve (for better perf & usability).
template <typename... Args>
error_or_stopped enrich_non_value(
    error_or_stopped eos,
    // The `format_string_and_location` doc explains the `type_identity`
    ext::format_string_and_location<std::type_identity_t<Args>...> snl = "",
    Args const&... args) {
  return error_or_stopped{rich_error<detail::enriched_non_value>{
      // No exception handling since `exception_ptr` creation currently is
      // `noexcept`, meaning that it either succeeds by using some "reserved
      // memory" if provided by the `std` implementation, or terminates.
      std::move(eos).release_rich_exception_ptr(),
      rich_msg{std::move(snl), args...}}};
}

template <typename T, typename... Args>
result<T> enrich_non_value(
    result<T> r,
    // The `error_or_stopped` overload explains the `type_identity`.
    ext::format_string_and_location<std::type_identity_t<Args>...> snl = "",
    Args const&... args) {
  if (r.has_value()) {
    return r;
  }
  return enrich_non_value(
      std::move(r).error_or_stopped(), std::move(snl), args...);
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
