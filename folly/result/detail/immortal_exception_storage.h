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
#include <folly/result/rich_error_fwd.h>

#include <exception>
#include <typeinfo>

#if FOLLY_HAS_RESULT

namespace folly::detail {

// `rich_exception_ptr`'s type-erased interface for immortal exceptions
class immortal_exception_storage {
 private:
  template <typename, typename>
  friend class rich_exception_ptr_impl;

  // Address of the immortal error instance -- not fully type-erased to allow
  // constexpr access to `rich_error_base` APIs (like codes).
  const rich_error_base* immortal_ptr_;

  const std::type_info* user_base_type_;
  const std::type_info* rich_error_leaf_type_;

  // Potentially cheaper than `std::rethrow_exception(to_exception_ptr())`
  // IMPORTANT: Mark any derived function `[[noreturn]]`, we assume this.
  virtual void throw_exception() const = 0;

  // Immutable access to a singleton that, unlike `to_exception_ptr()`,
  // permanently contains an unchanged copy of `*immortal_ptr_`.
  virtual const rich_error_base* as_immutable_leaf_rich_error() const = 0;
  virtual const std::exception* as_immutable_std_exception() const = 0;

  // Mutable access to the exception singleton from `to_exception_ptr()`.
  //
  // Must be `const`-qualified because `immortal_exception_storage` instances
  // are necessarily `constexpr`, but it is not LOGICALLY `const`.
  virtual rich_error_base* as_mutable_leaf_rich_error() const = 0;
  virtual std::exception* as_mutable_std_exception() const = 0;
  virtual std::exception_ptr* acquire_mutable_singleton_ptr_if_already_created()
      const = 0;

  // Undo type erasure: Returns a copy of a singleton `std::exception_ptr` of a
  // `rich_error<>`, which contains a copy of `*immortal_ptr_`.  Unlike the
  // `as_immutable_...` singleton, this copy CAN change.
  //
  // Note: Immortal exceptions are NEVER empty, by construction.
  virtual std::exception_ptr to_exception_ptr() const = 0;

 protected:
  // `consteval` guarantees the incoming pointer is immortal.
  consteval immortal_exception_storage(
      const rich_error_base* p,
      const std::type_info* me,
      const std::type_info* leaf)
      : immortal_ptr_{p}, user_base_type_{me}, rich_error_leaf_type_{leaf} {}

 public:
  constexpr virtual ~immortal_exception_storage() = default;
};

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
