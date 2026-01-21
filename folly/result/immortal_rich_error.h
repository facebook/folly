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
#include <folly/Traits.h>
#include <folly/result/detail/ptr_immortal_exception_storage.h>
#include <folly/result/detail/rich_error_common.h>
#include <folly/result/rich_exception_ptr.h>

#if FOLLY_HAS_RESULT

namespace folly {

template <typename, typename, auto...>
class immortal_rich_error_t;

namespace detail {

// Only this class and `rich_error` can instantiate `rich_error_base`!
template <typename UserBase>
class immortal_rich_error_storage final
    : public detail::rich_error_with_partial_message<UserBase> {
 private:
  template <typename, typename, auto...>
  friend class folly::immortal_rich_error_t;

  using detail::rich_error_with_partial_message<
      UserBase>::rich_error_with_partial_message;

  void only_rich_error_may_instantiate(
      rich_error_base::only_rich_error_may_instantiate_t) override {}

 public:
  // Don't use, only exposed for `ptr_immortal_exception_storage`
  using folly_private_immortal_rich_error_base = UserBase;
};

} // namespace detail

/// immortal_rich_error<T, CtorArgs...>
///
/// Demo usage:
///
///   struct MyErr : rich_error_base {
///     using folly_get_exception_hint_types = rich_error_hints<MyErr>;
///   };
///
///   static constexpr auto complex_arg = make_complex_arg();
///   inline constexpr rich_exception_ptr leet_my_err =
///       immortal_rich_error<MyErr, &complex_arg, 1337>::ptr();
///
/// In large programs, you will want to split definitions / declarations of the
/// `rich_exception_ptr` across `.cpp` / `.h` and mark them `constinit`.
///
/// If the ctor of `YourErr` takes `rich_msg`, you can pass string literals as
/// `immortal_rich_error<YourErr, "foo"_litv>` -- see examples in tests.
///
/// Implementation notes:
///   - The REP param is for tests. In prod, it is always `rich_exception_ptr`.
///   - This template supports taking `rich_error<MyErr>` to encourage end
///     users to alias their `rich_error`, and to hide the wrapper.  The goal?
///     -- just one identifier, `MyErr`, to remember.
///   - Taking the pointer as a template param ensures it's immortal.
///   - Per https://timsong-cpp.github.io/cppwp/n4868/temp.arg.nontype,
///     template params cannot point to sliced subobjects.  This avoids the
///     slicing risk from the `IS_RICH_ERROR_BASE` doc.
template <typename REP /* = rich_exception_ptr */, typename T, auto... Args>
class immortal_rich_error_t final {
 private:
  // Make `immortal_rich_error<rich_error<T>>` work as `immortal_rich_error<T>`.
  using UserBase =
      detected_or_t<T, detail::detect_folly_detail_base_of_rich_error, T>;
  // Since `std::exception` isn't constexpr until C++26, we can only
  // instantiate the user-provided base at compile-time.  That's all we need
  // for most operations on your immortal `rich_exception_ptr rep`.
  //
  // These two accesses lazily create an immutable, indestructible singleton
  // with a `rich_error<UserBase>` copy of your error:
  //   - `get_exception<std::exception>(rep)`
  //   - `get_exception<rich_error<UserBase>>(rep)`
  //
  // You will get a DIFFERENT mutable, indestructible singleton if you do:
  //   - `rep.to_exception_ptr_slow()`
  //   - `get_mutable_exception<Ex>(rep)`
  //
  // To recap: `const` operations on immortals always access the `constexpr`
  // exception, or a copy -- but, mutable operations use a separate singleton.
  constexpr static detail::immortal_rich_error_storage<UserBase> immortal_{
      Args...};
  constexpr static detail::ptr_immortal_exception_storage<&immortal_>
      storage_{};
  // Future: In applications with MANY errors, or where binary size is at a
  // premium, we could construct this bit at runtime at low cost.  This is also
  // a path to supporting non-rich immortals, if needed.
  constexpr static REP ptr_{&immortal_, &storage_};

 public:
  [[nodiscard]] constexpr static const REP& ptr() {
    // Has a manual test, but the build doesn't get this far.
    detail::static_assert_is_valid_rich_error_type<UserBase>(&immortal_);
    return ptr_;
  }
};
template <typename T, auto... Args>
inline constexpr immortal_rich_error_t<rich_exception_ptr, T, Args...>
    immortal_rich_error;

} // namespace folly

#endif // FOLLY_HAS_RESULT
