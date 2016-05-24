/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <folly/io/async/HHWheelTimer.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/test/UndelayedDestruction.h>
#include <folly/io/async/test/Util.h>

#include <gtest/gtest.h>
#include <vector>

using namespace folly;
using std::chrono::milliseconds;

typedef UndelayedDestruction<HHWheelTimer> StackWheelTimer;

class TestTimeout : public HHWheelTimer::Callback {
 public:
  TestTimeout() {}
  TestTimeout(HHWheelTimer* t, milliseconds timeout) {
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


class TestTimeoutDelayed : public TestTimeout {
 protected:
    std::chrono::milliseconds getCurTime() override {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()) -
        milliseconds(5);
    }
};

struct HHWheelTimerTest : public ::testing::Test {
  EventBase eventBase;
};

/*
 * Test firing some simple timeouts that are fired once and never rescheduled
 */
TEST_F(HHWheelTimerTest, FireOnce) {
  StackWheelTimer t(&eventBase, milliseconds(1));

  TestTimeout t1;
  TestTimeout t2;
  TestTimeout t3;

  ASSERT_EQ(t.count(), 0);

  t.scheduleTimeout(&t1, milliseconds(5));
  t.scheduleTimeout(&t2, milliseconds(5));
  // Verify scheduling it twice cancels, then schedules.
  // Should only get one callback.
  t.scheduleTimeout(&t2, milliseconds(5));
  t.scheduleTimeout(&t3, milliseconds(10));

  ASSERT_EQ(t.count(), 3);

  TimePoint start;
  eventBase.loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t3.timestamps.size(), 1);

  ASSERT_EQ(t.count(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(start, t2.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(start, t3.timestamps[0], milliseconds(10));
  T_CHECK_TIMEOUT(start, end, milliseconds(10));
}

/*
 * Test scheduling a timeout from another timeout callback.
 */
TEST_F(HHWheelTimerTest, TestSchedulingWithinCallback) {
  StackWheelTimer t(&eventBase, milliseconds(10));

  TestTimeout t1;
  // Delayed to simulate the steady_clock counter lagging
  TestTimeoutDelayed t2;

  t.scheduleTimeout(&t1, milliseconds(500));
  t1.fn = [&] { t.scheduleTimeout(&t2, milliseconds(1)); };
  // If t is in an inconsistent state, detachEventBase should fail.
  t2.fn = [&] { t.detachEventBase(); };

  ASSERT_EQ(t.count(), 1);

  eventBase.loop();

  ASSERT_EQ(t.count(), 0);
  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
}

/*
 * Test cancelling a timeout when it is scheduled to be fired right away.
 */

TEST_F(HHWheelTimerTest, CancelTimeout) {
  StackWheelTimer t(&eventBase, milliseconds(1));

  // Create several timeouts that will all fire in 5ms.
  TestTimeout t5_1(&t, milliseconds(5));
  TestTimeout t5_2(&t, milliseconds(5));
  TestTimeout t5_3(&t, milliseconds(5));
  TestTimeout t5_4(&t, milliseconds(5));
  TestTimeout t5_5(&t, milliseconds(5));

  // Also create a few timeouts to fire in 10ms
  TestTimeout t10_1(&t, milliseconds(10));
  TestTimeout t10_2(&t, milliseconds(10));
  TestTimeout t10_3(&t, milliseconds(10));

  TestTimeout t20_1(&t, milliseconds(20));
  TestTimeout t20_2(&t, milliseconds(20));

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
    t.scheduleTimeout(&t5_3, milliseconds(5));

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
  eventBase.loop();
  TimePoint end;

  ASSERT_EQ(t5_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_1.timestamps[0], milliseconds(5));

  ASSERT_EQ(t5_3.timestamps.size(), 2);
  T_CHECK_TIMEOUT(start, t5_3.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(t5_3.timestamps[0], t5_3.timestamps[1], milliseconds(5));

  ASSERT_EQ(t10_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t10_1.timestamps[0], milliseconds(10));
  ASSERT_EQ(t10_3.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t10_3.timestamps[0], milliseconds(10));

  // Cancelled timeouts
  ASSERT_EQ(t5_2.timestamps.size(), 0);
  ASSERT_EQ(t5_4.timestamps.size(), 0);
  ASSERT_EQ(t5_5.timestamps.size(), 0);
  ASSERT_EQ(t10_2.timestamps.size(), 0);
  ASSERT_EQ(t20_1.timestamps.size(), 0);
  ASSERT_EQ(t20_2.timestamps.size(), 0);

  T_CHECK_TIMEOUT(start, end, milliseconds(10));
}

/*
 * Test destroying a HHWheelTimer with timeouts outstanding
 */

TEST_F(HHWheelTimerTest, DestroyTimeoutSet) {
  HHWheelTimer::UniquePtr t(
      HHWheelTimer::newTimer(&eventBase, milliseconds(1)));

  TestTimeout t5_1(t.get(), milliseconds(5));
  TestTimeout t5_2(t.get(), milliseconds(5));
  TestTimeout t5_3(t.get(), milliseconds(5));

  TestTimeout t10_1(t.get(), milliseconds(10));
  TestTimeout t10_2(t.get(), milliseconds(10));

  // Have t5_2 destroy t
  // Note that this will call destroy() inside t's timeoutExpired()
  // method.
  t5_2.fn = [&] {
    t5_3.cancelTimeout();
    t5_1.cancelTimeout();
    t10_1.cancelTimeout();
    t10_2.cancelTimeout();
    t.reset();};

  TimePoint start;
  eventBase.loop();
  TimePoint end;

  ASSERT_EQ(t5_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_1.timestamps[0], milliseconds(5));
  ASSERT_EQ(t5_2.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_2.timestamps[0], milliseconds(5));

  ASSERT_EQ(t5_3.timestamps.size(), 0);
  ASSERT_EQ(t10_1.timestamps.size(), 0);
  ASSERT_EQ(t10_2.timestamps.size(), 0);

  T_CHECK_TIMEOUT(start, end, milliseconds(5));
}

/*
 * Test the tick interval parameter
 */
TEST_F(HHWheelTimerTest, AtMostEveryN) {

  // Create a timeout set with a 10ms interval, to fire no more than once
  // every 3ms.
  milliseconds interval(25);
  milliseconds atMostEveryN(6);
  StackWheelTimer t(&eventBase, atMostEveryN);
  t.setCatchupEveryN(70);

  // Create 60 timeouts to be added to ts1 at 1ms intervals.
  uint32_t numTimeouts = 60;
  std::vector<TestTimeout> timeouts(numTimeouts);

  // Create a scheduler timeout to add the timeouts 1ms apart.
  uint32_t index = 0;
  StackWheelTimer ts1(&eventBase, milliseconds(1));
  TestTimeout scheduler(&ts1, milliseconds(1));
  scheduler.fn = [&] {
    if (index >= numTimeouts) {
      return;
    }
    // Call timeoutExpired() on the timeout so it will record a timestamp.
    // This is done only so we can record when we scheduled the timeout.
    // This way if ts1 starts to fall behind a little over time we will still
    // be comparing the ts1 timeouts to when they were first scheduled (rather
    // than when we intended to schedule them).  The scheduler may fall behind
    // eventually since we don't really schedule it once every millisecond.
    // Each time it finishes we schedule it for 1 millisecond in the future.
    // The amount of time it takes to run, and any delays it encounters
    // getting scheduled may eventually add up over time.
    timeouts[index].timeoutExpired();

    // Schedule the new timeout
    t.scheduleTimeout(&timeouts[index], interval);
    // Reschedule ourself
    ts1.scheduleTimeout(&scheduler, milliseconds(1));
    ++index;
  };

  // Go ahead and schedule the first timeout now.
  //scheduler.fn();

  TimePoint start;
  eventBase.loop();
  TimePoint end;

  // This should take roughly 2*60 + 25 ms to finish. If it takes more than
  // 250 ms to finish the system is probably heavily loaded, so skip.
  if (std::chrono::duration_cast<std::chrono::milliseconds>(
        end.getTime() - start.getTime()).count() > 250) {
    LOG(WARNING) << "scheduling all timeouts takes too long";
    return;
  }

  // We scheduled timeouts 1ms apart, when the HHWheelTimer is only allowed
  // to wake up at most once every 3ms.  It will therefore wake up every 3ms
  // and fire groups of approximately 3 timeouts at a time.
  //
  // This is "approximately 3" since it may get slightly behind and fire 4 in
  // one interval, etc.  T_CHECK_TIMEOUT normally allows a few milliseconds of
  // tolerance.  We have to add the same into our checking algorithm here.
  for (uint32_t idx = 0; idx < numTimeouts; ++idx) {
    ASSERT_EQ(timeouts[idx].timestamps.size(), 2);

    TimePoint scheduledTime(timeouts[idx].timestamps[0]);
    TimePoint firedTime(timeouts[idx].timestamps[1]);

    // Assert that the timeout fired at roughly the right time.
    // T_CHECK_TIMEOUT() normally has a tolerance of 5ms.  Allow an additional
    // atMostEveryN.
    milliseconds tolerance = milliseconds(5) + interval;
    T_CHECK_TIMEOUT(scheduledTime, firedTime, atMostEveryN, tolerance);

    // Assert that the difference between the previous timeout and now was
    // either very small (fired in the same event loop), or larger than
    // atMostEveryN.
    if (idx == 0) {
      // no previous value
      continue;
    }
    TimePoint prev(timeouts[idx - 1].timestamps[1]);

    auto delta = (firedTime.getTimeStart() - prev.getTimeEnd()) -
      (firedTime.getTimeWaiting() - prev.getTimeWaiting());
    if (delta > milliseconds(1)) {
      T_CHECK_TIMEOUT(prev, firedTime, atMostEveryN);
    }
  }
}

/*
 * Test an event loop that is blocking
 */

TEST_F(HHWheelTimerTest, SlowLoop) {
  StackWheelTimer t(&eventBase, milliseconds(1));

  TestTimeout t1;
  TestTimeout t2;

  ASSERT_EQ(t.count(), 0);

  eventBase.runInLoop([](){usleep(10000);});
  t.scheduleTimeout(&t1, milliseconds(5));

  ASSERT_EQ(t.count(), 1);

  TimePoint start;
  eventBase.loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t.count(), 0);

  // Check that the timeout was delayed by sleep
  T_CHECK_TIMEOUT(start, t1.timestamps[0], milliseconds(15), milliseconds(1));
  T_CHECK_TIMEOUT(start, end, milliseconds(15), milliseconds(1));

  // Try it again, this time with catchup timing every loop
  t.setCatchupEveryN(1);

  eventBase.runInLoop([](){usleep(10000);});
  t.scheduleTimeout(&t2, milliseconds(5));

  ASSERT_EQ(t.count(), 1);

  TimePoint start2;
  eventBase.loop();
  TimePoint end2;

  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t.count(), 0);

  // Check that the timeout was NOT delayed by sleep
  T_CHECK_TIMEOUT(start2, t2.timestamps[0], milliseconds(10), milliseconds(1));
  T_CHECK_TIMEOUT(start2, end2, milliseconds(10), milliseconds(1));
}

/*
 * Test scheduling a mix of timers with default timeout and variable timeout.
 */
TEST_F(HHWheelTimerTest, DefaultTimeout) {
  milliseconds defaultTimeout(milliseconds(5));
  StackWheelTimer t(&eventBase,
                    milliseconds(1),
                    AsyncTimeout::InternalEnum::NORMAL,
                    defaultTimeout);

  TestTimeout t1;
  TestTimeout t2;

  ASSERT_EQ(t.count(), 0);
  ASSERT_EQ(t.getDefaultTimeout(), defaultTimeout);

  t.scheduleTimeout(&t1);
  t.scheduleTimeout(&t2, milliseconds(10));

  ASSERT_EQ(t.count(), 2);

  TimePoint start;
  eventBase.loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);

  ASSERT_EQ(t.count(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], defaultTimeout);
  T_CHECK_TIMEOUT(start, t2.timestamps[0], milliseconds(10));
  T_CHECK_TIMEOUT(start, end, milliseconds(10));
}

TEST_F(HHWheelTimerTest, lambda) {
  StackWheelTimer t(&eventBase, milliseconds(1));
  size_t count = 0;
  t.scheduleTimeoutFn([&]{ count++; }, milliseconds(1));
  eventBase.loop();
  EXPECT_EQ(1, count);
}

// shouldn't crash because we swallow and log the error (you'll have to look
// at the console to confirm logging)
TEST_F(HHWheelTimerTest, lambdaThrows) {
  StackWheelTimer t(&eventBase, milliseconds(1));
  t.scheduleTimeoutFn([&]{ throw std::runtime_error("expected"); },
                      milliseconds(1));
  eventBase.loop();
}

TEST_F(HHWheelTimerTest, cancelAll) {
  StackWheelTimer t(&eventBase);
  TestTimeout tt;
  t.scheduleTimeout(&tt, std::chrono::minutes(1));
  EXPECT_EQ(1, t.cancelAll());
  EXPECT_EQ(1, tt.canceledTimestamps.size());
}

TEST_F(HHWheelTimerTest, IntrusivePtr) {
  HHWheelTimer::UniquePtr t(
      HHWheelTimer::newTimer(&eventBase, milliseconds(1)));

  TestTimeout t1;
  TestTimeout t2;
  TestTimeout t3;

  ASSERT_EQ(t->count(), 0);

  t->scheduleTimeout(&t1, milliseconds(5));
  t->scheduleTimeout(&t2, milliseconds(5));

  DelayedDestruction::IntrusivePtr<HHWheelTimer> s(t);

  s->scheduleTimeout(&t3, milliseconds(10));

  ASSERT_EQ(t->count(), 3);

  // Kill the UniquePtr, but the SharedPtr keeps it alive
  t.reset();

  TimePoint start;
  eventBase.loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t3.timestamps.size(), 1);

  ASSERT_EQ(s->count(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(start, t2.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(start, t3.timestamps[0], milliseconds(10));
  T_CHECK_TIMEOUT(start, end, milliseconds(10));
}
