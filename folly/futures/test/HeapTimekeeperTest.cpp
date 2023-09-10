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

#include <folly/futures/HeapTimekeeper.h>
#include <folly/futures/test/TimekeeperTestLib.h>

namespace folly {

INSTANTIATE_TYPED_TEST_SUITE_P(
    HeapTimekeeperTest, TimekeeperTest, HeapTimekeeper);

TEST(TimekeeperSingletonTest, ExpectedType) {
  // This is just to check that the un-mocked default timekeeper singleton
  // Implementation is covered by some instantiation of the test suite. If the
  // default implementation is changed this test should be moved accordingly.
  ASSERT_TRUE(
      dynamic_cast<HeapTimekeeper*>(detail::getTimekeeperSingleton().get()) !=
      nullptr);
}

} // namespace folly
