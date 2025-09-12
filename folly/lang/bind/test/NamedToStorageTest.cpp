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

#include <folly/lang/bind/NamedToStorage.h>
#include <folly/portability/GTest.h>

//
// IMPORTANT: This is intended to parallel `folly/lang/test/BindTest.cpp`!
// To reduce redundancy, we don't repeat some of the tests here.
//

namespace folly::bind::ext {

// This is here so that test "runs" show up in CI history
TEST(NamedTest, all_tests_run_at_build_time) {}

template <typename BT>
auto get_policy(tag_t<BT>) -> bind_to_storage_policy<BT>;

template <typename BA>
using policy = decltype(get_policy(typename BA::binding_list_t{}));

// A minimal test that `storage_type` matches standard policy
static_assert(
    std::is_same_v<policy<decltype(const_ref(5))>::storage_type, const int&&>);

template <typename BA>
using sig = policy<BA>::signature_type;

constexpr auto check_in_place_binding_signature_type() {
  static_assert(
      std::is_same_v<
          sig<decltype("x"_id = constant(5))>,
          id_type<"x", const int>>);
  static_assert(
      std::is_same_v<sig<decltype("x"_id = mut(5))>, id_type<"x", int>>);
  static_assert(
      std::is_same_v<sig<decltype("x"_id = mut_ref(5))>, id_type<"x", int&&>>);
  static_assert(
      std::is_same_v<
          sig<decltype(self_id = constant(in_place<int>(5)))>,
          self_id_type<const int>>);

  return true;
}

static_assert(check_in_place_binding_signature_type());

} // namespace folly::bind::ext
