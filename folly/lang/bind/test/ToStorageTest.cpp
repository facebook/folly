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

#include <folly/lang/bind/ToStorage.h>
#include <folly/portability/GTest.h>

namespace folly::bind::ext {

// This is here so that test "runs" show up in CI history
TEST(ToStorageTest, all_tests_run_at_build_time) {}

template <typename B>
using first_policy =
    bind_to_storage_policy<type_list_element_t<0, typename B::binding_list_t>>;

// A minimal test for `using signature_type = storage_type`...
static_assert(
    std::is_same_v<
        typename first_policy<decltype(constant(const_ref(5)))>::signature_type,
        const int&&>);

template <typename B>
using store = typename first_policy<B>::storage_type;

constexpr auto check_in_place_binding_storage_type() {
  int b = 2;

  static_assert(std::is_same_v<store<decltype(constant(b))>, const int>);
  static_assert(std::is_same_v<store<decltype(mut(b))>, int>);
  static_assert(std::is_same_v<store<decltype(constant(5))>, const int>);
  static_assert(std::is_same_v<store<decltype(mut(5))>, int>);

  static_assert(std::is_same_v<store<decltype(const_ref(b))>, const int&>);
  static_assert(
      std::is_same_v<store<decltype(constant(const_ref(b)))>, const int&>);
  static_assert(std::is_same_v<store<decltype(mut_ref(b))>, int&>);
  static_assert(std::is_same_v<store<decltype(const_ref(5))>, const int&&>);
  static_assert(
      std::is_same_v<store<decltype(constant(const_ref(5)))>, const int&&>);
  static_assert(std::is_same_v<store<decltype(mut_ref(5))>, int&&>);

  static_assert(
      std::is_same_v<
          first_policy<decltype(in_place<int>(5))>::storage_type,
          int>);
  static_assert(
      std::is_same_v<store<decltype(constant(in_place<int>(5)))>, const int>);

  return true;
}

static_assert(check_in_place_binding_storage_type());

} // namespace folly::bind::ext
