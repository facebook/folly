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

#include <folly/detail/UniqueInstance.h>

#include <tuple>

#include <folly/String.h>
#include <folly/Traits.h>
#include <folly/portability/GTest.h>

struct Key1 {};
struct Key2 {};
struct TagA {};
struct TagB {};

namespace folly {
namespace detail {

class UniqueInstanceDeathTest : public testing::Test {};

template <typename... Key, typename... Mapped>
static void make(char const* tmpl, tag_t<Key...> key, tag_t<Mapped...> mapped) {
  UniqueInstance _(tmpl, key, mapped);
  std::ignore = _;
}

TEST_F(UniqueInstanceDeathTest, basic) {
  constexpr auto const tname = "tname";
  make(tname, tag_t<Key1, Key2>{}, tag_t<TagA, TagB>{});
  make(tname, tag_t<Key1, Key2>{}, tag_t<TagA, TagB>{}); // same everything
  make(tname, tag_t<Key2, Key1>{}, tag_t<TagA, TagB>{}); // different key
  EXPECT_DEATH( // different name
      make("wrong", tag_t<Key1, Key2>{}, tag_t<TagA, TagB>{}),
      stripLeftMargin(R"MESSAGE(
        Overloaded unique instance over <Key1, Key2, ...> with differing trailing arguments:
          tname<Key1, Key2, TagA, TagB>
          wrong<Key1, Key2, TagA, TagB>
      )MESSAGE"));
  EXPECT_DEATH( // different mapped
      make(tname, tag_t<Key1, Key2>{}, tag_t<TagB, TagA>{}),
      stripLeftMargin(R"MESSAGE(
        Overloaded unique instance over <Key1, Key2, ...> with differing trailing arguments:
          tname<Key1, Key2, TagA, TagB>
          tname<Key1, Key2, TagB, TagA>
      )MESSAGE"));
}

} // namespace detail
} // namespace folly
