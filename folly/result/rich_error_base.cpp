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

#include <folly/result/rich_error_base.h>
#include <folly/result/rich_exception_ptr.h>

#include <folly/Demangle.h>
#include <folly/lang/SafeAssert.h>

#include <ostream>

#if FOLLY_HAS_RESULT

namespace folly {

source_location rich_error_base::source_location() const noexcept {
  // The content of the default-constructed class is unspecified by the
  // standard, but ...  libstdc++, libc++, and MSVC all default to an empty
  // name.  So, rather than make this `std::optional`, our `format_to` omits
  // the location if `sl.file_name() != source_location{}.file_name()`.
  return {};
}

const rich_exception_ptr* rich_error_base::next_error_for_epitaph()
    const noexcept {
  return nullptr;
}

std::ostream& operator<<(std::ostream& os, const rich_error_base& e) {
  // NOLINTNEXTLINE(facebook-hte-DetailCall)
  return detail::ostream_write_via_fmt(os, e);
}

void rich_error_base::format_to(fmt::appender& out) const {
  auto msg = partial_message();
  fmt::format_to(out, "{}", msg);
  auto fmt_codes = all_codes_for_fmt(msg[0] ? " - " : "");
  fmt::format_to(out, "{}", fmt_codes);
  auto sl = source_location();
  // See `rich_error_base::source_location()` for the rationale
  if (sl.file_name() != folly::source_location{}.file_name()) {
    // Omit the separator if the previous steps had no output.
    if (msg[0] || fmt_codes.saw_code_) {
      fmt::format_to(out, " @ ");
    }
    fmt::format_to(out, "{}:{}", sl.file_name(), sl.line());
  }
}

namespace {

struct FormatterState {
  rich_error_base::private_get_exception_ptr_type_t priv_;

  // Multiple stacked epitaph wrappers can share the same underlying error,
  // but we only want to show that underlying error once.
  const rich_exception_ptr* seen_underlying_rep_ = nullptr;
  // Deduplicating `rich_exception_ptr*` is not enough -- when printing an
  // underlying `rich_error_base*`, there might NOT EXIST an original
  // `rich_exception_ptr*` to compare against.
  const rich_error_base* seen_underlying_rex_ = nullptr;
  bool need_after_separator_ = false;

  explicit FormatterState(rich_error_base::private_get_exception_ptr_type_t p)
      : priv_{p} {}

  void mark_underlying_as_seen(
      const rich_exception_ptr* rep, const rich_error_base* rex) {
    seen_underlying_rep_ = rep;
    seen_underlying_rex_ = rex;
  }

  void format_separator_if_needed(fmt::appender& out) {
    if (need_after_separator_) {
      fmt::format_to(out, " [after] ");
      need_after_separator_ = false;
    }
  }

  void format_rich(fmt::appender& out, const rich_error_base* rex) {
    format_separator_if_needed(out);
    rex->format_to(out);
  }

  void format_non_rich(fmt::appender& out, const rich_exception_ptr& rep) {
    format_separator_if_needed(out);
    const char* type_name = rep.exception_type(priv_)->name();
    decltype(folly::demangle(type_name)) demangled;
    // Demangling requires* an allocation, but we really don't want formatting
    // to throw since it may make debugging OOMs painful.
    //
    // *If you need async-signal-safety, then try-allocate-catch is no good.
    // `folly::demangle` also has a fixed-buffer version, for which you could
    // allocate a few dozen bytes on stack here.
    try {
      demangled = folly::demangle(type_name);
      type_name = demangled.c_str();
    } catch (...) {
    }
    if (const auto* ex = rep.get_outer_exception<std::exception>()) {
      fmt::format_to(out, "{}: {}", type_name, ex->what());
    } else {
      fmt::format_to(out, "{}", type_name);
    }
  }

  // Handle an epitaph wrapper -- outputs "underlying [via] wrapper"
  void format_underlying_and_epitaph(
      fmt::appender& out, const rich_error_base* wrapper) {
    auto* underlying_rep = wrapper->underlying_error();
    // Format the underlying error first, if we haven't already
    if (seen_underlying_rep_ != underlying_rep) {
      if (auto* underlying_rex =
              underlying_rep->get_outer_exception<rich_error_base>()) {
        if (seen_underlying_rex_ != underlying_rex) {
          format_rich(out, underlying_rex);
        }
        mark_underlying_as_seen(underlying_rep, underlying_rex);
      } else {
        format_non_rich(out, *underlying_rep);
        mark_underlying_as_seen(underlying_rep, nullptr);
      }
      fmt::format_to(out, " [via] ");
    }
    format_rich(out, wrapper);
  }

  void format_epitaph_stack_entry(
      fmt::appender& out, const rich_error_base* current) {
    if (current->underlying_error()) { // Epitaph wrapper
      format_underlying_and_epitaph(out, current);
    } else if (seen_underlying_rex_ != current) { // Underlying error
      format_rich(out, current);
      mark_underlying_as_seen(nullptr, current);
    } // Else: underlying was already formatted
  }

  // Precondition: `current` was already formatted.
  void format_rest_of_chain(fmt::appender& out, const rich_error_base* rex) {
    while (const auto* next_rep = rex->next_error_for_epitaph()) {
      rex = next_rep->get_outer_exception<rich_error_base>();
      need_after_separator_ = true;
      if (!rex) { // Non-rich, making it both last AND underlying.
        if (seen_underlying_rep_ != next_rep) {
          format_non_rich(out, *next_rep);
        }
        return;
      }
      format_epitaph_stack_entry(out, rex);
    }
  }
};

} // namespace

// Format this epitaph stack, starting with its underlying error.
// Outputs: "Underlying [via] OuterWrapper [after] InnerWrapper"
void rich_error_base::format_with_epitaphs(fmt::appender& out) const {
  FormatterState state{private_get_exception_ptr_type_t{}};
  state.format_epitaph_stack_entry(out, this);
  state.format_rest_of_chain(out, this);
}

// Format only the epitaph stack (caller had already formatted underlying).
// Precondition: `this` is a wrapper, not an underlying error.
// Outputs: " [via] OuterWrapper [after] InnerWrapper"
void rich_error_base::format_with_epitaphs_without_first_underlying(
    fmt::appender& out) const {
  FormatterState state{private_get_exception_ptr_type_t{}};
  { // Mark the first underlying as already printed (by the caller)
    auto* rep = underlying_error();
    FOLLY_SAFE_DCHECK(rep); // See precondition; falls back to `rex == this`
    state.mark_underlying_as_seen(
        rep, rep ? rep->get_outer_exception<rich_error_base>() : this);
  }
  // By the precondition, `this` wraps the already-printed underlying.
  fmt::format_to(out, " [via] ");
  this->format_to(out);
  state.format_rest_of_chain(out, this);
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
