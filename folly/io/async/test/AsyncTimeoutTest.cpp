/*
 * Copyright 2017 Facebook, Inc.
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

#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GTest.h>

namespace folly {

TEST(AsyncTimeout, make) {
  int value = 0;
  int const expected = 10;
  EventBase manager;

  auto observer = AsyncTimeout::make(
    manager,
    [&]() noexcept { value = expected; }
  );

  observer->scheduleTimeout(std::chrono::milliseconds(100));

  manager.loop();

  EXPECT_EQ(expected, value);
}

TEST(AsyncTimeout, schedule) {
  int value = 0;
  int const expected = 10;
  EventBase manager;

  auto observer = AsyncTimeout::schedule(
    std::chrono::milliseconds(100),
    manager,
    [&]() noexcept { value = expected; }
  );

  manager.loop();

  EXPECT_EQ(expected, value);
}

TEST(AsyncTimeout, cancel_make) {
  int value = 0;
  int const expected = 10;
  EventBase manager;

  auto observer = AsyncTimeout::make(
    manager,
    [&]() noexcept { value = expected; }
  );

  observer->scheduleTimeout(std::chrono::milliseconds(100));
  observer->cancelTimeout();

  manager.loop();

  EXPECT_NE(expected, value);
}

TEST(AsyncTimeout, cancel_schedule) {
  int value = 0;
  int const expected = 10;
  EventBase manager;

  auto observer = AsyncTimeout::schedule(
    std::chrono::milliseconds(100),
    manager,
    [&]() noexcept { value = expected; }
  );

  observer->cancelTimeout();

  manager.loop();

  EXPECT_NE(expected, value);
}

} // namespace folly {
