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

#include <folly/lang/BitFields.h>

#include <folly/portability/GTest.h>

class BitFieldsTest : public testing::Test {};

TEST_F(BitFieldsTest, BasicUsage) {
  // Start by verifying example from bit_fields comment
  struct MyState : public folly::bit_fields<uint32_t, MyState> {};

  using Field1 =
      folly::unsigned_bit_field<MyState, 16, folly::no_prev_bit_field>;
  using Field2 = folly::bool_bit_field<MyState, Field1>;
  using Field3 = folly::bool_bit_field<MyState, Field2>;
  using Field4a = folly::unsigned_bit_field<MyState, 5, Field3>;
  // Can support variant bit fields, at your own risk
  using Field4b = folly::bool_bit_field<MyState, Field3>;
  using Field5b = folly::unsigned_bit_field<MyState, 4, Field4b>;

  auto state =
      MyState{}.with<Field1>(45U).with<Field2>(true).with<Field3>(true);
  state.set<Field4a>(3U);

  EXPECT_EQ(state.get<Field1>(), 45U);
  EXPECT_EQ(state.get<Field2>(), true);
  EXPECT_EQ(state.get<Field3>(), true);
  EXPECT_EQ(state.get<Field4a>(), 3U);

  // As if Field3 indicates which variant is used for remaining fields
  state.set<Field3>(false);
  state.set<Field4b>(true);
  state.set<Field5b>(5U);

  EXPECT_EQ(state.get<Field1>(), 45U);
  EXPECT_EQ(state.get<Field2>(), true);
  EXPECT_EQ(state.get<Field3>(), false);
  EXPECT_EQ(state.get<Field4b>(), true);
  EXPECT_EQ(state.get<Field5b>(), 5U);

  MyState state2;
  EXPECT_NE(state, state2);
  state.set<Field2>(false);
  state.set<Field4b>(false);
  state.set<Field5b>(0U);
  EXPECT_NE(state, state2);
  state.set<Field1>(0U);
  // Back to all zeros
  EXPECT_EQ(state, state2);

  // Misc operators
  auto ref = state.ref<Field3>();
  auto ref2 = std::move(ref);
  ref2 = true;
  EXPECT_EQ(state.get<Field3>(), true);
  auto ref3 = state.ref<Field1>();
  ref3 = 40U;
  EXPECT_EQ(state.get<Field1>(), 40U);
  ref3 += 5U;
  EXPECT_EQ(state.get<Field1>(), 45U);
  ref3 -= 38U;
  EXPECT_EQ(state.get<Field1>(), 7U);
  ref3 |= 17U;
  EXPECT_EQ(state.get<Field1>(), 23U);
  ref3 &= 48U;
  EXPECT_EQ(state.get<Field1>(), 16U);
}
