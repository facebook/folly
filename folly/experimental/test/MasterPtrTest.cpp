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

#include <future>

#include <folly/experimental/MasterPtr.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

TEST(MasterPtrTest, Basic) {
  auto ptr = std::make_unique<int>(42);
  auto rawPtr = ptr.get();

  folly::MasterPtr<int> masterPtr(std::move(ptr));
  auto masterPtrRef = masterPtr.ref();

  auto lockedPtr1 = masterPtr.lock();
  auto lockedPtr2 = masterPtrRef.lock();
  EXPECT_EQ(lockedPtr1.get(), rawPtr);
  EXPECT_EQ(lockedPtr2.get(), rawPtr);

  EXPECT_EQ(lockedPtr1.use_count(), 3);
  EXPECT_EQ(lockedPtr2.use_count(), 3);

  auto joinFuture = std::async(std::launch::async, [&] { masterPtr.join(); });

  auto lockFailFuture = std::async(std::launch::async, [&] {
    while (masterPtr.lock()) {
      std::this_thread::yield();
    }
  });

  EXPECT_EQ(
      lockFailFuture.wait_for(std::chrono::milliseconds{100}),
      std::future_status::ready);

  EXPECT_EQ(lockedPtr1.use_count(), 2);
  EXPECT_EQ(lockedPtr2.use_count(), 2);

  EXPECT_EQ(masterPtr.lock().get(), nullptr);
  EXPECT_EQ(masterPtrRef.lock().get(), nullptr);

  EXPECT_EQ(
      joinFuture.wait_for(std::chrono::milliseconds{100}),
      std::future_status::timeout);

  lockedPtr1.reset();
  lockedPtr2.reset();

  EXPECT_EQ(
      joinFuture.wait_for(std::chrono::milliseconds{100}),
      std::future_status::ready);
}
