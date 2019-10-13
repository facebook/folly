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

#include <folly/experimental/TimerFDTimeoutManager.h>
#include <folly/io/async/test/Util.h>
#include <folly/portability/GTest.h>

using namespace folly;
using std::chrono::microseconds;

class TestTimeout : public TimerFDTimeoutManager::Callback {
 public:
  TestTimeout() {}
  TestTimeout(TimerFDTimeoutManager* t, microseconds timeout) {
    t->scheduleTimeout(this, timeout);
  }

  void timeoutExpired() noexcept override {
    timestamps.emplace_back();
    if (fn) {
      fn();
    }
  }

  void callbackCanceled() noexcept override {
    canceledTimestamps.emplace_back();
    if (fn) {
      fn();
    }
  }

  std::deque<TimePoint> timestamps;
  std::deque<TimePoint> canceledTimestamps;
  std::function<void()> fn;
};

struct TimerFDTimeoutManagerTest : public ::testing::Test {
  TimerFDTimeoutManagerTest() : timeoutMgr(&evb) {}

  EventBase evb;
  TimerFDTimeoutManager timeoutMgr;
};

/*
 * Test firing some simple timeouts that are fired once and never rescheduled
 */
TEST_F(TimerFDTimeoutManagerTest, FireOnce) {
  TimerFDTimeoutManager& t = timeoutMgr;

  TestTimeout t1;
  TestTimeout t2;
  TestTimeout t3;

  ASSERT_EQ(t.count(), 0);

  t.scheduleTimeout(&t1, microseconds(50));
  t.scheduleTimeout(&t2, microseconds(50));
  // Verify scheduling it twice cancels, then schedules.
  // Should only get one callback.
  t.scheduleTimeout(&t2, microseconds(50));
  t.scheduleTimeout(&t3, microseconds(100));

  ASSERT_EQ(t.count(), 3);

  TestTimeout ts;
  ts.fn = [&]() { evb.terminateLoopSoon(); };
  t.scheduleTimeout(&ts, microseconds(1000));

  TimePoint start;
  evb.loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t3.timestamps.size(), 1);

  ASSERT_EQ(t.count(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], microseconds(500));
  T_CHECK_TIMEOUT(start, t2.timestamps[0], microseconds(500));
  T_CHECK_TIMEOUT(start, t3.timestamps[0], microseconds(10));
  T_CHECK_TIMEOUT(start, end, microseconds(10));
}

/*
 * Test scheduling a timeout from another timeout callback.
 */
TEST_F(TimerFDTimeoutManagerTest, TestSchedulingWithinCallback) {
  TimerFDTimeoutManager& t = timeoutMgr;

  TestTimeout t1, t2;

  t.scheduleTimeout(&t1, microseconds(500 * 1000));
  t1.fn = [&] {
    t.scheduleTimeout(&t2, microseconds(1000));
    std::this_thread::sleep_for(std::chrono::microseconds(5000));
  };

  t2.fn = [&] { evb.terminateLoopSoon(); };

  ASSERT_EQ(t.count(), 1);

  evb.loop();

  ASSERT_EQ(t.count(), 0);
  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
}

/*
 * Test cancelling a timeout when it is scheduled to be fired right away.
 */

TEST_F(TimerFDTimeoutManagerTest, CancelTimeout) {
  TimerFDTimeoutManager& t = timeoutMgr;

  // Create several timeouts that will all fire in 5microseconds
  TestTimeout t5_1(&t, microseconds(50));
  TestTimeout t5_2(&t, microseconds(50));
  TestTimeout t5_3(&t, microseconds(50));
  TestTimeout t5_4(&t, microseconds(50));
  TestTimeout t5_5(&t, microseconds(50));

  // Also create a few timeouts to fire in 10microseconds
  TestTimeout t10_1(&t, microseconds(100));
  TestTimeout t10_2(&t, microseconds(100));
  TestTimeout t10_3(&t, microseconds(100));

  TestTimeout t20_1(&t, microseconds(200));
  TestTimeout t20_2(&t, microseconds(200));

  TestTimeout et(&t, microseconds(1000));
  et.fn = [&] { evb.terminateLoopSoon(); };

  // Have t5_1 cancel t5_2 and t5_4.
  //
  // Cancelling t5_2 will test cancelling a timeout that is at the head of the
  // list and ready to be fired.
  //
  // Cancelling t5_4 will test cancelling a timeout in the middle of the list
  t5_1.fn = [&] {
    t5_2.cancelTimeout();
    t5_4.cancelTimeout();
  };

  // Have t5_3 cancel t5_5.
  // This will test cancelling the last remaining timeout.
  //
  // Then have t5_3 reschedule itself.
  t5_3.fn = [&] {
    t5_5.cancelTimeout();
    // Reset our function so we won't continually reschedule ourself
    std::function<void()> fnDtorGuard;
    t5_3.fn.swap(fnDtorGuard);
    t.scheduleTimeout(&t5_3, microseconds(500));

    // Also test cancelling timeouts in another timeset that isn't ready to
    // fire yet.
    //
    // Cancel the middle timeout in ts10.
    t10_2.cancelTimeout();
    // Cancel both the timeouts in ts20.
    t20_1.cancelTimeout();
    t20_2.cancelTimeout();
  };

  TimePoint start;
  evb.loop();
  TimePoint end;

  ASSERT_EQ(t5_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_1.timestamps[0], microseconds(500));

  ASSERT_EQ(t5_3.timestamps.size(), 2);
  T_CHECK_TIMEOUT(start, t5_3.timestamps[0], microseconds(500));
  T_CHECK_TIMEOUT(t5_3.timestamps[0], t5_3.timestamps[1], microseconds(500));

  ASSERT_EQ(t10_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t10_1.timestamps[0], microseconds(10));
  ASSERT_EQ(t10_3.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t10_3.timestamps[0], microseconds(10));

  // Cancelled timeouts
  ASSERT_EQ(t5_2.timestamps.size(), 0);
  ASSERT_EQ(t5_4.timestamps.size(), 0);
  ASSERT_EQ(t5_5.timestamps.size(), 0);
  ASSERT_EQ(t10_2.timestamps.size(), 0);
  ASSERT_EQ(t20_1.timestamps.size(), 0);
  ASSERT_EQ(t20_2.timestamps.size(), 0);

  T_CHECK_TIMEOUT(start, end, microseconds(10));
}

/*
 * Test destroying a TimerFDTimeoutManager with timeouts outstanding
 */

TEST_F(TimerFDTimeoutManagerTest, DestroyTimeoutSet) {
  TimerFDTimeoutManager::UniquePtr t(new TimerFDTimeoutManager(&evb));

  TestTimeout t5_1(t.get(), microseconds(50));
  TestTimeout t5_2(t.get(), microseconds(50));
  TestTimeout t5_3(t.get(), microseconds(50));

  TestTimeout t10_1(t.get(), microseconds(100));
  TestTimeout t10_2(t.get(), microseconds(100));

  // Have t5_2 destroy t
  // Note that this will call destroy() inside t's timeoutExpired()
  // method.
  t5_2.fn = [&] {
    t5_3.cancelTimeout();
    t5_1.cancelTimeout();
    t10_1.cancelTimeout();
    t10_2.cancelTimeout();
    t.reset();
    evb.terminateLoopSoon();
  };

  TimePoint start;
  evb.loop();
  TimePoint end;

  ASSERT_EQ(t5_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_1.timestamps[0], microseconds(500));
  ASSERT_EQ(t5_2.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_2.timestamps[0], microseconds(500));

  ASSERT_EQ(t5_3.timestamps.size(), 0);
  ASSERT_EQ(t10_1.timestamps.size(), 0);
  ASSERT_EQ(t10_2.timestamps.size(), 0);

  T_CHECK_TIMEOUT(start, end, microseconds(500));
}

/*
 * Test an event scheduled before the last event fires on time
 */
TEST_F(TimerFDTimeoutManagerTest, SlowFast) {
  TimerFDTimeoutManager& t = timeoutMgr;

  TestTimeout t1;
  TestTimeout t2;

  ASSERT_EQ(t.count(), 0);

  t.scheduleTimeout(&t1, microseconds(100));
  t.scheduleTimeout(&t2, microseconds(50));

  ASSERT_EQ(t.count(), 2);

  TestTimeout et(&t, microseconds(1000));
  et.fn = [&] { evb.terminateLoopSoon(); };

  TimePoint start;
  evb.loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t.count(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], microseconds(100));
  T_CHECK_TIMEOUT(start, t2.timestamps[0], microseconds(50));
}

TEST_F(TimerFDTimeoutManagerTest, ReschedTest) {
  TimerFDTimeoutManager& t = timeoutMgr;

  TestTimeout t1;
  TestTimeout t2;

  ASSERT_EQ(t.count(), 0);

  t.scheduleTimeout(&t1, microseconds(128));
  TimePoint start2;
  t1.fn = [&]() {
    t.scheduleTimeout(&t2, microseconds(255)); // WHEEL_SIZE - 1
    start2.reset();
    ASSERT_EQ(t.count(), 2); // we scheduled et
  };

  ASSERT_EQ(t.count(), 1);

  TestTimeout et(&t, microseconds(1000));
  et.fn = [&] { evb.terminateLoopSoon(); };

  TimePoint start;
  evb.loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t.count(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], microseconds(128));
  T_CHECK_TIMEOUT(start2, t2.timestamps[0], microseconds(255));
}

TEST_F(TimerFDTimeoutManagerTest, DeleteTimeoutManagerInTimeout) {
  TimerFDTimeoutManager::UniquePtr t(new TimerFDTimeoutManager(&evb));

  TestTimeout t1;
  TestTimeout t2;
  TestTimeout t3;

  ASSERT_EQ(t->count(), 0);

  t->scheduleTimeout(&t1, microseconds(128));
  t->scheduleTimeout(&t2, microseconds(128));
  t->scheduleTimeout(&t3, microseconds(128));
  t1.fn = [&]() { t2.cancelTimeout(); };
  t3.fn = [&]() {
    t.reset();
    evb.terminateLoopSoon();
  };

  ASSERT_EQ(t->count(), 3);

  TimePoint start;
  evb.loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], microseconds(128));
}

TEST_F(TimerFDTimeoutManagerTest, lambda) {
  TimerFDTimeoutManager& t = timeoutMgr;
  size_t count = 0;
  t.scheduleTimeoutFn([&] { count++; }, microseconds(100));

  TestTimeout et(&t, microseconds(1000));
  et.fn = [&] { evb.terminateLoopSoon(); };

  evb.loop();
  EXPECT_EQ(1, count);
}

// shouldn't crash because we swallow and log the error (you'll have to look
// at the console to confirm logging)
TEST_F(TimerFDTimeoutManagerTest, lambdaThrows) {
  TimerFDTimeoutManager& t = timeoutMgr;
  size_t count = 0;
  t.scheduleTimeoutFn(
      [&] {
        count++;
        throw std::runtime_error("expected");
      },
      microseconds(100));
  TestTimeout et(&t, microseconds(1000));
  et.fn = [&] { evb.terminateLoopSoon(); };

  evb.loop();
  // make sure the callback was invoked
  EXPECT_EQ(1, count);
}

TEST_F(TimerFDTimeoutManagerTest, cancelAll) {
  TimerFDTimeoutManager& t = timeoutMgr;
  TestTimeout t1;
  TestTimeout t2;
  t.scheduleTimeout(&t1, std::chrono::microseconds(1000));
  t.scheduleTimeout(&t2, std::chrono::microseconds(1000));
  size_t canceled = 0;
  t1.fn = [&] { canceled += t.cancelAll(); };
  t2.fn = [&] { canceled += t.cancelAll(); };
  // Sleep 20ms to ensure both timeouts will fire in a single event (in case
  // they ended up in different slots)
  ::usleep(20000);

  evb.scheduleAt(
      [&]() { evb.terminateLoopSoon(); },
      std::chrono::steady_clock::now() + std::chrono::milliseconds(100));

  evb.loop();
  EXPECT_EQ(1, t1.canceledTimestamps.size() + t2.canceledTimestamps.size());
  EXPECT_EQ(1, canceled);
}

TEST_F(TimerFDTimeoutManagerTest, IntrusivePtr) {
  TimerFDTimeoutManager::UniquePtr t(new TimerFDTimeoutManager(&evb));

  TestTimeout t1;
  TestTimeout t2;
  TestTimeout t3;

  ASSERT_EQ(t->count(), 0);

  t->scheduleTimeout(&t1, microseconds(500));
  t->scheduleTimeout(&t2, microseconds(500));

  DelayedDestruction::IntrusivePtr<TimerFDTimeoutManager> s(t);

  s->scheduleTimeout(&t3, microseconds(10));

  ASSERT_EQ(t->count(), 3);

  // Kill the UniquePtr, but the SharedPtr keeps it alive
  t.reset();

  evb.scheduleAt(
      [&]() { evb.terminateLoopSoon(); },
      std::chrono::steady_clock::now() + std::chrono::milliseconds(10));

  TimePoint start;
  evb.loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t3.timestamps.size(), 1);

  ASSERT_EQ(s->count(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], microseconds(500));
  T_CHECK_TIMEOUT(start, t2.timestamps[0], microseconds(500));
  T_CHECK_TIMEOUT(start, t3.timestamps[0], microseconds(10));
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(10)); // this is right
}

TEST_F(TimerFDTimeoutManagerTest, GetTimeRemaining) {
  TimerFDTimeoutManager& t = timeoutMgr;
  TestTimeout t1;

  // Not scheduled yet, time remaining should be zero
  ASSERT_EQ(t1.getTimeRemaining(), microseconds(0));
  ASSERT_EQ(t.count(), 0);

  // Scheduled, time remaining should be less than or equal to the scheduled
  // timeout
  t.scheduleTimeout(&t1, microseconds(10));
  ASSERT_LE(t1.getTimeRemaining(), microseconds(10));
  t1.fn = [&] { evb.terminateLoopSoon(); };

  TimePoint start;
  evb.loop();
  TimePoint end;

  // Expired and time remaining should be zero
  ASSERT_EQ(t1.getTimeRemaining(), microseconds(0));

  ASSERT_EQ(t.count(), 0);
  T_CHECK_TIMEOUT(start, end, microseconds(10));
}

// Test that we handle negative timeouts properly (i.e. treat them as 0)
TEST_F(TimerFDTimeoutManagerTest, NegativeTimeout) {
  TimerFDTimeoutManager& t = timeoutMgr;
  std::this_thread::sleep_for(std::chrono::microseconds(10));
  TestTimeout tt1;
  TestTimeout tt2;
  // Make sure we have event scheduled.
  t.scheduleTimeout(&tt1, std::chrono::microseconds(100));
  // Schedule another timeout that would appear to be earlier than
  // the already scheduled one.
  t.scheduleTimeout(&tt2, std::chrono::microseconds(-500000000));

  evb.scheduleAt(
      [&]() { evb.terminateLoopSoon(); },
      std::chrono::steady_clock::now() + std::chrono::milliseconds(1));
  TimePoint start;
  evb.loop();
  TimePoint end;
  ASSERT_EQ(tt2.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(10));
}
