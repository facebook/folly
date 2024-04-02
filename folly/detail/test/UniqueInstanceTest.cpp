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

#include <folly/detail/UniqueInstance.h>

#include <tuple>

#include <folly/String.h>
#include <folly/Traits.h>
#include <folly/portability/GTest.h>

struct Key1;
struct Key2;
struct TagA;
struct TagB;

template <typename...>
struct Template0 {};
template <typename...>
struct Template1 {};

namespace folly {
namespace detail {

class UniqueInstanceDeathTest : public testing::Test {};

template <template <typename...> class Z, typename... Key, typename... Mapped>
static void make(tag_t<Key...> key, tag_t<Mapped...> mapped) {
  std::ignore = UniqueInstance{tag<Z<Key..., Mapped...>>, key, mapped};
}

TEST_F(UniqueInstanceDeathTest, basic) {
  make<Template0>(tag<Key1, Key2>, tag<TagA, TagB>);
  make<Template0>(tag<Key1, Key2>, tag<TagA, TagB>); // same everything
  make<Template1>(tag<Key1, Key2>, tag<TagA, TagB>); // different name
  make<Template0>(tag<Key2, Key1>, tag<TagA, TagB>); // different key
  EXPECT_DEATH( // different mapped
      make<Template0>(tag<Key1, Key2>, tag<TagB, TagA>),
      stripLeftMargin(R"MESSAGE(
        Overloaded unique instance over <Key1, Key2, ...> with differing trailing arguments:
          Template0<Key1, Key2, TagA, TagB>
          Template0<Key1, Key2, TagB, TagA>
      )MESSAGE"));
}

} // namespace detail
} // namespace folly
