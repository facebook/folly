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

#include <folly/synchronization/AtomicBitFields.h>

#include <folly/portability/GTest.h>

class BitFieldsAtomicTest : public testing::Test {};

struct MyState : public folly::bit_fields<uint32_t, MyState> {};

TEST_F(BitFieldsAtomicTest, Basic) {
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

  folly::atomic_bit_fields<MyState> atomic{state};
  EXPECT_EQ(state, atomic.load());
  EXPECT_NE(state, state2);
  atomic.store(state2);
  EXPECT_EQ(state2, atomic.load());
  MyState state3 = atomic.exchange(state);
  EXPECT_EQ(state2, state3);
  EXPECT_TRUE(atomic.compare_exchange_strong(state, state2));
  while (!atomic.compare_exchange_weak(state2, state)) {
  }
  EXPECT_EQ(state2, state3);
  MyState state4 = atomic; // Implicit conversion
  EXPECT_EQ(state, state4);
  atomic = state2; // operator=
  EXPECT_EQ(state2, atomic.load());
}

TEST_F(BitFieldsAtomicTest, CopyConstructor) {
  using Field1 =
      folly::unsigned_bit_field<MyState, 16, folly::no_prev_bit_field>;
  using Field2 = folly::bool_bit_field<MyState, Field1>;

  auto state = MyState{}.with<Field1>(123U).with<Field2>(true);
  folly::atomic_bit_fields<MyState> atomic1{state};

  folly::atomic_bit_fields<MyState> atomic2{atomic1};
  EXPECT_EQ(atomic1.load(), atomic2.load());
  EXPECT_EQ(state, atomic2.load());
}

TEST_F(BitFieldsAtomicTest, CopyAssignment) {
  using Field1 =
      folly::unsigned_bit_field<MyState, 16, folly::no_prev_bit_field>;
  using Field2 = folly::bool_bit_field<MyState, Field1>;

  auto state1 = MyState{}.with<Field1>(100U).with<Field2>(true);
  auto state2 = MyState{}.with<Field1>(200U).with<Field2>(false);

  folly::atomic_bit_fields<MyState> atomic1{state1};
  folly::atomic_bit_fields<MyState> atomic2{state2};

  EXPECT_NE(atomic1.load(), atomic2.load());

  atomic2 = atomic1;
  EXPECT_EQ(atomic1.load(), atomic2.load());
  EXPECT_EQ(state1, atomic2.load());
}

TEST_F(BitFieldsAtomicTest, DefaultConstructor) {
  folly::atomic_bit_fields<MyState> atomic;
  MyState defaultState;
  EXPECT_EQ(defaultState, atomic.load());
}

TEST_F(BitFieldsAtomicTest, Transforms) {
  using Field1 =
      folly::unsigned_bit_field<MyState, 16, folly::no_prev_bit_field>;
  using Field2 = folly::bool_bit_field<MyState, Field1>;
  using Field3 = folly::bool_bit_field<MyState, Field2>;
  using Field4 = folly::unsigned_bit_field<MyState, 5, Field3>;

  auto state =
      MyState{}.with<Field1>(45U).with<Field2>(true).with<Field3>(true);
  state.set<Field4>(3U);

  folly::atomic_bit_fields<MyState> atomic{state};

  auto transform1 = Field2::clearTransform() + Field3::clearTransform();
  MyState before, after;
  atomic.apply(transform1, std::memory_order_acq_rel, &before, &after);
  EXPECT_EQ(before, state);
  EXPECT_NE(after, state);
  EXPECT_EQ(after.get<Field2>(), false);
  EXPECT_EQ(after.get<Field3>(), false);

  auto transform2 = Field2::setTransform() + Field3::setTransform();
  atomic.apply(transform2, std::memory_order_acq_rel, &before, &after);
  EXPECT_NE(before, state);
  EXPECT_EQ(before.get<Field2>(), false);
  EXPECT_EQ(before.get<Field3>(), false);
  EXPECT_EQ(after, state);

  EXPECT_EQ(state.get<Field1>(), 45U);
  EXPECT_EQ(after.get<Field2>(), true);
  EXPECT_EQ(after.get<Field3>(), true);
  EXPECT_EQ(state.get<Field4>(), 3U);

  auto transform2a = Field2::andTransform(true) + Field3::andTransform(false);
  atomic.apply(transform2a, std::memory_order_acq_rel, &before, &after);
  EXPECT_EQ(after.get<Field2>(), true);
  EXPECT_EQ(after.get<Field3>(), false);

  auto transform2b = Field2::andTransform(false) + Field3::andTransform(true);
  atomic.apply(transform2b, std::memory_order_acq_rel, &before, &after);
  EXPECT_EQ(after.get<Field2>(), false);
  EXPECT_EQ(after.get<Field3>(), false);

  auto transform2c = Field2::orTransform(true) + Field3::orTransform(false);
  atomic.apply(transform2c, std::memory_order_acq_rel, &before, &after);
  EXPECT_EQ(after.get<Field2>(), true);
  EXPECT_EQ(after.get<Field3>(), false);

  auto transform2d = Field2::orTransform(false) + Field3::orTransform(true);
  atomic.apply(transform2d, std::memory_order_acq_rel, &before, &after);
  EXPECT_EQ(after.get<Field2>(), true);
  EXPECT_EQ(after.get<Field3>(), true);

  EXPECT_EQ(state.get<Field1>(), 45U);
  EXPECT_EQ(state.get<Field4>(), 3U);

  auto transform3 = Field1::plusTransformPromiseNoOverflow(10000U) +
      Field4::minusTransformPromiseNoUnderflow(3U);
  atomic.apply(transform3, std::memory_order_acq_rel, &before, &after);
  EXPECT_EQ(before, state);
  EXPECT_NE(after, state);
  EXPECT_EQ(after.get<Field1>(), 10045U);
  EXPECT_EQ(after.get<Field4>(), 0U);

  auto transform4 = Field1::minusTransformPromiseNoUnderflow(999U) +
      Field4::plusTransformPromiseNoOverflow(31U);
  atomic.apply(transform4, std::memory_order_acq_rel, &before, &after);
  EXPECT_EQ(after.get<Field1>(), 9046U);
  EXPECT_EQ(after.get<Field4>(), 31U);

  auto transform4a =
      Field1::andTransform(8192U + 4096U) + Field4::andTransform(15U);
  atomic.apply(transform4a, std::memory_order_acq_rel, &before, &after);
  EXPECT_EQ(after.get<Field1>(), 8192U);
  EXPECT_EQ(after.get<Field4>(), 15U);

  auto transform4b = Field1::orTransform(127U) + Field4::orTransform(16U);
  atomic.apply(transform4b, std::memory_order_acq_rel, &before, &after);
  EXPECT_EQ(after.get<Field1>(), 8192U + 127U);
  EXPECT_EQ(after.get<Field4>(), 31U);

  // Unmodified
  EXPECT_EQ(after.get<Field2>(), true);
  EXPECT_EQ(after.get<Field3>(), true);
}

TEST_F(BitFieldsAtomicTest, TopBitField) {
  using Field1 =
      folly::unsigned_bit_field<MyState, 16, folly::no_prev_bit_field>;
  using Field2 = folly::bool_bit_field<MyState, Field1>;
  using Field3 = folly::bool_bit_field<MyState, Field2>;
  using Field4 = folly::unsigned_bit_field<MyState, 5, Field3>;
  using Field5 = folly::unsigned_bit_field<MyState, 9, Field4>;

  folly::atomic_bit_fields<MyState> atomic{MyState{}};
  MyState before, after;

  // A field at the limit of upper bits is allowed to over/underflow
  atomic.store(MyState{}.with<Field5>(0)); // Field5 at 0
  atomic.apply(
      Field5::minusTransformIgnoreUnderflow(1U),
      std::memory_order_acq_rel,
      &before,
      &after); // "Safe" underflow
  EXPECT_EQ(after.get<Field5>(), 511U);
  atomic.apply(
      Field5::plusTransformIgnoreOverflow(1U),
      std::memory_order_acq_rel,
      &before,
      &after); // "Safe" overflow
  EXPECT_EQ(after.get<Field5>(), 0U);
  atomic.apply(
      Field5::plusTransformIgnoreOverflow(2048U),
      std::memory_order_acq_rel,
      &before,
      &after); // "Safe" overflow
  EXPECT_EQ(after.get<Field5>(), 0U);
}
