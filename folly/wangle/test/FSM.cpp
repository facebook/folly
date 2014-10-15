/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <folly/wangle/detail/FSM.h>

using namespace folly::wangle::detail;

enum class State { A, B };

TEST(FSM, ctor) {
  FSM<State> fsm(State::A);
  EXPECT_EQ(State::A, fsm.getState());
}

TEST(FSM, update) {
  FSM<State> fsm(State::A);
  EXPECT_TRUE(fsm.updateState(State::A, State::B, []{}));
  EXPECT_EQ(State::B, fsm.getState());
}

TEST(FSM, badUpdate) {
  FSM<State> fsm(State::A);
  EXPECT_FALSE(fsm.updateState(State::B, State::A, []{}));
}

TEST(FSM, actionOnUpdate) {
  FSM<State> fsm(State::A);
  size_t count = 0;
  fsm.updateState(State::A, State::B, [&]{ count++; });
  EXPECT_EQ(1, count);
}

TEST(FSM, noActionOnBadUpdate) {
  FSM<State> fsm(State::A);
  size_t count = 0;
  fsm.updateState(State::B, State::A, [&]{ count++; });
  EXPECT_EQ(0, count);
}

TEST(FSM, magicMacros) {
  struct MyFSM : public FSM<State> {
    size_t count = 0;
    MyFSM() : FSM<State>(State::A) {}
    void twiddle() {
      FSM_START
        FSM_UPDATE(State::A, State::B, [&]{ count++; });
        FSM_UPDATE(State::B, State::A, [&]{ count--; });
      FSM_END
    }
  };

  MyFSM fsm;

  fsm.twiddle();
  EXPECT_EQ(State::B, fsm.getState());
  EXPECT_EQ(1, fsm.count);

  fsm.twiddle();
  EXPECT_EQ(State::A, fsm.getState());
  EXPECT_EQ(0, fsm.count);
}

TEST(FSM, magicMacros2) {
  struct MyFSM : public FSM<State> {
    size_t count = 0;
    size_t count2 = 0;
    MyFSM() : FSM<State>(State::A) {}
    void twiddle() {
      FSM_START
        FSM_UPDATE2(State::A, State::B, [&]{ count++; }, count2++);
        FSM_UPDATE2(State::B, State::A, [&]{ count--; }, count2--);
      FSM_END
    }
  };

  MyFSM fsm;

  fsm.twiddle();
  EXPECT_EQ(State::B, fsm.getState());
  EXPECT_EQ(1, fsm.count);
  EXPECT_EQ(1, fsm.count2);

  fsm.twiddle();
  EXPECT_EQ(State::A, fsm.getState());
  EXPECT_EQ(0, fsm.count);
  EXPECT_EQ(0, fsm.count2);
}
