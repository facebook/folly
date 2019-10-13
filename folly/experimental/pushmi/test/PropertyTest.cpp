/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/experimental/pushmi/properties.h>

namespace {

struct foo_category;
struct bar_category;

struct big_foo_property {
  using property_category = foo_category;
};

struct small_foo_property {
  using property_category = foo_category;
};

struct fast_bar_property {
  using property_category = bar_category;
};

struct slow_bar_property {
  using property_category = bar_category;
};

} // namespace

using namespace folly::pushmi;

////////////////
// Tests for property_set_insert_t<PS1, PS2>

static_assert(
    std::is_same<
        property_set<big_foo_property>,
        property_set_insert_t<property_set<>, property_set<big_foo_property>>>::
        value, "");
static_assert(std::is_same<
              property_set<big_foo_property>,
              property_set_insert_t<
                  property_set<big_foo_property>,
                  property_set<big_foo_property>>>::value, "");
static_assert(
    std::is_same<
        property_set<big_foo_property>,
        property_set_insert_t<property_set<big_foo_property>, property_set<>>>::
        value, "");
static_assert(std::is_same<
              property_set<big_foo_property>,
              property_set_insert_t<
                  property_set<small_foo_property>,
                  property_set<big_foo_property>>>::value, "");
static_assert(std::is_same<
              property_set<big_foo_property, slow_bar_property>,
              property_set_insert_t<
                  property_set<big_foo_property, slow_bar_property>,
                  property_set<big_foo_property>>>::value, "");
static_assert(std::is_same<
              property_set<big_foo_property, slow_bar_property>,
              property_set_insert_t<
                  property_set<small_foo_property, slow_bar_property>,
                  property_set<big_foo_property>>>::value, "");
static_assert(std::is_same<
              property_set<slow_bar_property, big_foo_property>,
              property_set_insert_t<
                  property_set<slow_bar_property>,
                  property_set<big_foo_property>>>::value, "");

int main() {
  return 0;
}
