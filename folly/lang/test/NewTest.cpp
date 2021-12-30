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

#include <folly/lang/New.h>

#include <folly/lang/Align.h>
#include <folly/portability/GTest.h>

class NewTest : public testing::Test {};

TEST_F(NewTest, operator_new_delete) {
  constexpr auto s = std::size_t(256);
  constexpr auto a = std::align_val_t(folly::max_align_v);
  constexpr auto nt = std::nothrow;

  folly::operator_delete(folly::operator_new(s));
  folly::operator_delete(folly::operator_new(s), s);
  folly::operator_delete(folly::operator_new(s, a), a);
  folly::operator_delete(folly::operator_new(s, a), s, a);

  folly::operator_delete(folly::operator_new(s, nt));
  folly::operator_delete(folly::operator_new(s, nt), s);
  folly::operator_delete(folly::operator_new(s, a, nt), a);
  folly::operator_delete(folly::operator_new(s, a, nt), s, a);
}
