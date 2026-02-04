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

#include <folly/result/rich_exception_ptr.h>

#include <ostream>

#include <folly/Demangle.h>
#include <folly/lang/Switch.h>
#include <folly/result/rich_error_base.h>

#if FOLLY_HAS_RESULT

namespace folly {

void rich_exception_ptr::format_to(
    fmt::appender out, detail::format_to_skip_rich_t opts) const {
  using B = detail::rich_exception_ptr_base;
  FOLLY_NON_EXHAUSTIVE_SWITCH(switch (get_bits()) {
    case B::SIGIL_eq:
      fmt::format_to(out, "[empty Try]");
      return;

    case B::NOTHROW_OPERATION_CANCELLED_eq:
      fmt::format_to(out, "folly::detail::StoppedNoThrow");
      return;

      /* Future:
    case B::SMALL_VALUE_eq:
      fmt::format_to(out, "[small value]");
      return;
      */

    case B::IS_IMMORTAL_RICH_ERROR_OR_EMPTY_eq:
      if (!get_immortal_storage()) {
        fmt::format_to(out, "[empty]");
        return;
      }
      break;

    default:
      break;
  });

  // Rich error (immortal or owned)
  if (!opts.skip_) {
    if (auto* rex = get_outer_exception<rich_error_base>()) {
      rex->format_with_epitaphs(out);
      return;
    }
  }

  // Owned non-rich exception
  auto* type = exception_ptr_get_type(get_eptr_ref_guard().ref());

  // Regular exception: demangle + what()
  const char* type_name = type->name();
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
  if (auto* ex = get_outer_exception<std::exception>()) {
    fmt::format_to(out, "{}: {}", type_name, ex->what());
  } else {
    fmt::format_to(out, "{}", type_name);
  }
}

std::ostream& operator<<(std::ostream& os, const rich_exception_ptr& rep) {
  return detail::ostream_write_via_fmt(os, rep);
}

namespace detail {

const std::exception_ptr& bad_result_access_singleton() {
  // Union trick: the empty destructor ensures `value` is never destroyed.
  // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
  union Storage {
    std::exception_ptr value;
    Storage()
        : value{make_exception_ptr_with(
              std::in_place_type<bad_result_access_error>)} {}
    ~Storage() {}
  };

  // Meyer singleton is thread-safe past C++11. Leak it to avoid SDOF.
  static Storage storage;
  return storage.value;
}

} // namespace detail
} // namespace folly

#endif // FOLLY_HAS_RESULT
