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

#include <folly/wangle/futures/Timekeeper.h>
#include <unistd.h>

using namespace folly::wangle;
using namespace std::chrono;
using folly::wangle::Timekeeper;
using Duration = folly::wangle::Duration;

std::chrono::milliseconds const one_ms(1);
std::chrono::milliseconds const awhile(10);

std::chrono::steady_clock::time_point now() {
  return std::chrono::steady_clock::now();
}

struct TimekeeperFixture : public testing::Test {
  TimekeeperFixture() :
    timeLord_(folly::wangle::detail::getTimekeeperSingleton())
  {}

  Timekeeper* timeLord_;
};

TEST_F(TimekeeperFixture, after) {
  Duration waited(0);

  auto t1 = now();
  auto f = timeLord_->after(awhile);
  EXPECT_FALSE(f.isReady());
  f.get();
  auto t2 = now();

  EXPECT_GE(t2 - t1, awhile);
}

TEST(Timekeeper, futureGet) {
  Promise<int> p;
  std::thread([&]{ p.setValue(42); }).detach();
  EXPECT_EQ(42, p.getFuture().get());
}

TEST(Timekeeper, futureGetBeforeTimeout) {
  Promise<int> p;
  auto t = std::thread([&]{ p.setValue(42); });
  // Technically this is a race and if the test server is REALLY overloaded
  // and it takes more than a second to do that thread it could be flaky. But
  // I want a low timeout (in human terms) so if this regresses and someone
  // runs it by hand they're not sitting there forever wondering why it's
  // blocked, and get a useful error message instead. If it does get flaky,
  // empirically increase the timeout to the point where it's very improbable.
  EXPECT_EQ(42, p.getFuture().get(seconds(2)));
  t.join();
}

TEST(Timekeeper, futureGetTimeout) {
  Promise<int> p;
  EXPECT_THROW(p.getFuture().get(Duration(1)), folly::wangle::TimedOut);
}

TEST(Timekeeper, futureSleep) {
  auto t1 = now();
  futures::sleep(one_ms).get();
  EXPECT_GE(now() - t1, one_ms);
}

TEST(Timekeeper, futureDelayed) {
  auto t1 = now();
  auto dur = makeFuture()
    .delayed(one_ms)
    .then([=]{ return now() - t1; })
    .get();

  EXPECT_GE(dur, one_ms);
}

TEST(Timekeeper, futureWithinThrows) {
  Promise<int> p;
  auto f = p.getFuture()
    .within(one_ms)
    .onError([](TimedOut&) { return -1; });

  EXPECT_EQ(-1, f.get());
}

TEST(Timekeeper, futureWithinAlreadyComplete) {
  auto f = makeFuture(42)
    .within(one_ms)
    .onError([&](TimedOut&){ return -1; });

  EXPECT_EQ(42, f.get());
}

TEST(Timekeeper, futureWithinFinishesInTime) {
  Promise<int> p;
  auto f = p.getFuture()
    .within(std::chrono::minutes(1))
    .onError([&](TimedOut&){ return -1; });
  p.setValue(42);

  EXPECT_EQ(42, f.get());
}

TEST(Timekeeper, futureWithinVoidSpecialization) {
  makeFuture().within(one_ms);
}

TEST(Timekeeper, futureWithinException) {
  Promise<void> p;
  auto f = p.getFuture().within(awhile, std::runtime_error("expected"));
  EXPECT_THROW(f.get(), std::runtime_error);
}
