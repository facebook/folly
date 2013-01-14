/*
 * Copyright 2013 Facebook, Inc.
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

#include "folly/stats/BucketedTimeSeries.h"
#include "folly/stats/BucketedTimeSeries-defs.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "folly/Foreach.h"

using std::chrono::seconds;
using std::string;
using std::vector;
using folly::BucketedTimeSeries;

struct TestData {
  size_t duration;
  size_t numBuckets;
  vector<ssize_t> bucketStarts;
};
vector<TestData> testData = {
  // 71 seconds x 4 buckets
  { 71, 4, {0, 18, 36, 54}},
  // 100 seconds x 10 buckets
  { 100, 10, {0, 10, 20, 30, 40, 50, 60, 70, 80, 90}},
  // 10 seconds x 10 buckets
  { 10, 10, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}},
  // 10 seconds x 1 buckets
  { 10, 1, {0}},
  // 1 second x 1 buckets
  { 1, 1, {0}},
};

TEST(BucketedTimeSeries, getBucketInfo) {
  for (const auto& data : testData) {
    BucketedTimeSeries<int64_t> ts(data.numBuckets, seconds(data.duration));

    for (uint32_t n = 0; n < 10000; n += 1234) {
      seconds offset(n * data.duration);

      for (uint32_t idx = 0; idx < data.numBuckets; ++idx) {
        seconds bucketStart(data.bucketStarts[idx]);
        seconds nextBucketStart;
        if (idx + 1 < data.numBuckets) {
            nextBucketStart = seconds(data.bucketStarts[idx + 1]);
        } else {
            nextBucketStart = seconds(data.duration);
        }

        seconds expectedStart = offset + bucketStart;
        seconds expectedNextStart = offset + nextBucketStart;
        seconds midpoint = (expectedStart + expectedNextStart) / 2;

        vector<std::pair<string, seconds>> timePoints = {
          {"expectedStart", expectedStart},
          {"midpoint", midpoint},
          {"expectedEnd", expectedNextStart - seconds(1)},
        };

        for (const auto& point : timePoints) {
          // Check that getBucketIdx() returns the expected index
          EXPECT_EQ(idx, ts.getBucketIdx(point.second)) <<
            data.duration << "x" << data.numBuckets << ": " <<
            point.first << "=" << point.second.count();

          // Check the data returned by getBucketInfo()
          size_t returnedIdx;
          seconds returnedStart;
          seconds returnedNextStart;
          ts.getBucketInfo(expectedStart, &returnedIdx,
                           &returnedStart, &returnedNextStart);
          EXPECT_EQ(idx, returnedIdx) <<
            data.duration << "x" << data.numBuckets << ": " <<
            point.first << "=" << point.second.count();
          EXPECT_EQ(expectedStart.count(), returnedStart.count()) <<
            data.duration << "x" << data.numBuckets << ": " <<
            point.first << "=" << point.second.count();
          EXPECT_EQ(expectedNextStart.count(), returnedNextStart.count()) <<
            data.duration << "x" << data.numBuckets << ": " <<
            point.first << "=" << point.second.count();
        }
      }
    }
  }
}

void testUpdate100x10(size_t offset) {
  // This test code only works when offset is a multiple of the bucket width
  CHECK_EQ(0, offset % 10);

  // Create a 100 second timeseries, with 10 buckets
  BucketedTimeSeries<int64_t> ts(10, seconds(100));

  auto setup = [&] {
    ts.clear();
    // Add 1 value to each bucket
    for (int n = 5; n <= 95; n += 10) {
      ts.addValue(seconds(n + offset), 6);
    }

    EXPECT_EQ(10, ts.count());
    EXPECT_EQ(60, ts.sum());
    EXPECT_EQ(6, ts.avg());
  };

  // Update 2 buckets forwards.  This should throw away 2 data points.
  setup();
  ts.update(seconds(110 + offset));
  EXPECT_EQ(8, ts.count());
  EXPECT_EQ(48, ts.sum());
  EXPECT_EQ(6, ts.avg());

  // The last time we added was 95.
  // Try updating to 189.  This should clear everything but the last bucket.
  setup();
  ts.update(seconds(151 + offset));
  EXPECT_EQ(4, ts.count());
  //EXPECT_EQ(6, ts.sum());
  EXPECT_EQ(6, ts.avg());

  // The last time we added was 95.
  // Try updating to 193: This is nearly one full loop around,
  // back to the same bucket.  update() needs to clear everything
  setup();
  ts.update(seconds(193 + offset));
  EXPECT_EQ(0, ts.count());
  EXPECT_EQ(0, ts.sum());
  EXPECT_EQ(0, ts.avg());

  // The last time we added was 95.
  // Try updating to 197: This is slightly over one full loop around,
  // back to the same bucket.  update() needs to clear everything
  setup();
  ts.update(seconds(197 + offset));
  EXPECT_EQ(0, ts.count());
  EXPECT_EQ(0, ts.sum());
  EXPECT_EQ(0, ts.avg());

  // The last time we added was 95.
  // Try updating to 230: This is well over one full loop around,
  // and everything should be cleared.
  setup();
  ts.update(seconds(230 + offset));
  EXPECT_EQ(0, ts.count());
  EXPECT_EQ(0, ts.sum());
  EXPECT_EQ(0, ts.avg());
}

TEST(BucketedTimeSeries, update100x10) {
  // Run testUpdate100x10() multiple times, with various offsets.
  // This makes sure the update code works regardless of which bucket it starts
  // at in the modulo arithmetic.
  testUpdate100x10(0);
  testUpdate100x10(50);
  testUpdate100x10(370);
  testUpdate100x10(1937090);
}

TEST(BucketedTimeSeries, update71x5) {
  // Create a 71 second timeseries, with 5 buckets
  // This tests when the number of buckets does not divide evenly into the
  // duration.
  BucketedTimeSeries<int64_t> ts(5, seconds(71));

  auto setup = [&] {
    ts.clear();
    // Add 1 value to each bucket
    ts.addValue(seconds(11), 6);
    ts.addValue(seconds(24), 6);
    ts.addValue(seconds(42), 6);
    ts.addValue(seconds(43), 6);
    ts.addValue(seconds(66), 6);

    EXPECT_EQ(5, ts.count());
    EXPECT_EQ(30, ts.sum());
    EXPECT_EQ(6, ts.avg());
  };

  // Update 2 buckets forwards.  This should throw away 2 data points.
  setup();
  ts.update(seconds(99));
  EXPECT_EQ(3, ts.count());
  EXPECT_EQ(18, ts.sum());
  EXPECT_EQ(6, ts.avg());

  // Update 3 buckets forwards.  This should throw away 3 data points.
  setup();
  ts.update(seconds(100));
  EXPECT_EQ(2, ts.count());
  EXPECT_EQ(12, ts.sum());
  EXPECT_EQ(6, ts.avg());

  // Update 4 buckets forwards, just under the wrap limit.
  // This should throw everything but the last bucket away.
  setup();
  ts.update(seconds(127));
  EXPECT_EQ(1, ts.count());
  EXPECT_EQ(6, ts.sum());
  EXPECT_EQ(6, ts.avg());

  // Update 5 buckets forwards, exactly at the wrap limit.
  // This should throw everything away.
  setup();
  ts.update(seconds(128));
  EXPECT_EQ(0, ts.count());
  EXPECT_EQ(0, ts.sum());
  EXPECT_EQ(0, ts.avg());

  // Update very far forwards, wrapping multiple times.
  // This should throw everything away.
  setup();
  ts.update(seconds(1234));
  EXPECT_EQ(0, ts.count());
  EXPECT_EQ(0, ts.sum());
  EXPECT_EQ(0, ts.avg());
}

TEST(BucketedTimeSeries, elapsed) {
  BucketedTimeSeries<int64_t> ts(60, seconds(600));

  // elapsed() is 0 when no data points have been added
  EXPECT_EQ(0, ts.elapsed().count());

  // With exactly 1 data point, elapsed() should report 1 second of data
  seconds start(239218);
  ts.addValue(start + seconds(0), 200);
  EXPECT_EQ(1, ts.elapsed().count());
  // Adding a data point 10 seconds later should result in an elapsed time of
  // 11 seconds (the time range is [0, 10], inclusive).
  ts.addValue(start + seconds(10), 200);
  EXPECT_EQ(11, ts.elapsed().count());

  // elapsed() returns to 0 after clear()
  ts.clear();
  EXPECT_EQ(0, ts.elapsed().count());

  // Restart, with the starting point on an easier number to work with
  ts.addValue(seconds(10), 200);
  EXPECT_EQ(1, ts.elapsed().count());
  ts.addValue(seconds(580), 200);
  EXPECT_EQ(571, ts.elapsed().count());
  ts.addValue(seconds(590), 200);
  EXPECT_EQ(581, ts.elapsed().count());
  ts.addValue(seconds(598), 200);
  EXPECT_EQ(589, ts.elapsed().count());
  ts.addValue(seconds(599), 200);
  EXPECT_EQ(590, ts.elapsed().count());
  ts.addValue(seconds(600), 200);
  EXPECT_EQ(591, ts.elapsed().count());
  ts.addValue(seconds(608), 200);
  EXPECT_EQ(599, ts.elapsed().count());
  ts.addValue(seconds(609), 200);
  EXPECT_EQ(600, ts.elapsed().count());
  // Once we reach 600 seconds worth of data, when we move on to the next
  // second a full bucket will get thrown out.  Now we drop back down to 591
  // seconds worth of data
  ts.addValue(seconds(610), 200);
  EXPECT_EQ(591, ts.elapsed().count());
  ts.addValue(seconds(618), 200);
  EXPECT_EQ(599, ts.elapsed().count());
  ts.addValue(seconds(619), 200);
  EXPECT_EQ(600, ts.elapsed().count());
  ts.addValue(seconds(620), 200);
  EXPECT_EQ(591, ts.elapsed().count());
  ts.addValue(seconds(123419), 200);
  EXPECT_EQ(600, ts.elapsed().count());
  ts.addValue(seconds(123420), 200);
  EXPECT_EQ(591, ts.elapsed().count());
  ts.addValue(seconds(123425), 200);
  EXPECT_EQ(596, ts.elapsed().count());

  // Time never moves backwards.
  // Calling update with an old timestamp will just be ignored.
  ts.update(seconds(29));
  EXPECT_EQ(596, ts.elapsed().count());
}

TEST(BucketedTimeSeries, rate) {
  BucketedTimeSeries<int64_t> ts(60, seconds(600));

  // Add 3 values every 2 seconds, until fill up the buckets
  for (size_t n = 0; n < 600; n += 2) {
    ts.addValue(seconds(n), 200, 3);
  }

  EXPECT_EQ(900, ts.count());
  EXPECT_EQ(180000, ts.sum());
  EXPECT_EQ(200, ts.avg());

  // Really we only entered 599 seconds worth of data: [0, 598] (inclusive)
  EXPECT_EQ(599, ts.elapsed().count());
  EXPECT_NEAR(300.5, ts.rate(), 0.005);
  EXPECT_NEAR(1.5, ts.countRate(), 0.005);

  // If we add 1 more second, now we will have 600 seconds worth of data
  ts.update(seconds(599));
  EXPECT_EQ(600, ts.elapsed().count());
  EXPECT_NEAR(300, ts.rate(), 0.005);
  EXPECT_EQ(300, ts.rate<int>());
  EXPECT_NEAR(1.5, ts.countRate(), 0.005);

  // However, 1 more second after that and we will have filled up all the
  // buckets, and have to drop one.
  ts.update(seconds(600));
  EXPECT_EQ(591, ts.elapsed().count());
  EXPECT_NEAR(299.5, ts.rate(), 0.01);
  EXPECT_EQ(299, ts.rate<int>());
  EXPECT_NEAR(1.5, ts.countRate(), 0.005);
}

TEST(BucketedTimeSeries, avgTypeConversion) {
  // Make sure the computed average values are accurate regardless
  // of the input type and return type.

  {
    // Simple sanity tests for small positive integer values
    BucketedTimeSeries<int64_t> ts(60, seconds(600));
    ts.addValue(seconds(0), 4, 100);
    ts.addValue(seconds(0), 10, 200);
    ts.addValue(seconds(0), 16, 100);

    EXPECT_DOUBLE_EQ(10.0, ts.avg());
    EXPECT_DOUBLE_EQ(10.0, ts.avg<float>());
    EXPECT_EQ(10, ts.avg<uint64_t>());
    EXPECT_EQ(10, ts.avg<int64_t>());
    EXPECT_EQ(10, ts.avg<int32_t>());
    EXPECT_EQ(10, ts.avg<int16_t>());
    EXPECT_EQ(10, ts.avg<int8_t>());
    EXPECT_EQ(10, ts.avg<uint8_t>());
  }

  {
    // Test signed integer types with negative values
    BucketedTimeSeries<int64_t> ts(60, seconds(600));
    ts.addValue(seconds(0), -100);
    ts.addValue(seconds(0), -200);
    ts.addValue(seconds(0), -300);
    ts.addValue(seconds(0), -200, 65535);

    EXPECT_DOUBLE_EQ(-200.0, ts.avg());
    EXPECT_DOUBLE_EQ(-200.0, ts.avg<float>());
    EXPECT_EQ(-200, ts.avg<int64_t>());
    EXPECT_EQ(-200, ts.avg<int32_t>());
    EXPECT_EQ(-200, ts.avg<int16_t>());
  }

  {
    // Test uint64_t values that would overflow int64_t
    BucketedTimeSeries<uint64_t> ts(60, seconds(600));
    ts.addValueAggregated(seconds(0),
                          std::numeric_limits<uint64_t>::max(),
                          std::numeric_limits<uint64_t>::max());

    EXPECT_DOUBLE_EQ(1.0, ts.avg());
    EXPECT_DOUBLE_EQ(1.0, ts.avg<float>());
    EXPECT_EQ(1, ts.avg<uint64_t>());
    EXPECT_EQ(1, ts.avg<int64_t>());
    EXPECT_EQ(1, ts.avg<int8_t>());
  }

  {
    // Test doubles with small-ish values that will fit in integer types
    BucketedTimeSeries<double> ts(60, seconds(600));
    ts.addValue(seconds(0), 4.0, 100);
    ts.addValue(seconds(0), 10.0, 200);
    ts.addValue(seconds(0), 16.0, 100);

    EXPECT_DOUBLE_EQ(10.0, ts.avg());
    EXPECT_DOUBLE_EQ(10.0, ts.avg<float>());
    EXPECT_EQ(10, ts.avg<uint64_t>());
    EXPECT_EQ(10, ts.avg<int64_t>());
    EXPECT_EQ(10, ts.avg<int32_t>());
    EXPECT_EQ(10, ts.avg<int16_t>());
    EXPECT_EQ(10, ts.avg<int8_t>());
    EXPECT_EQ(10, ts.avg<uint8_t>());
  }

  {
    // Test doubles with huge values
    BucketedTimeSeries<double> ts(60, seconds(600));
    ts.addValue(seconds(0), 1e19, 100);
    ts.addValue(seconds(0), 2e19, 200);
    ts.addValue(seconds(0), 3e19, 100);

    EXPECT_DOUBLE_EQ(ts.avg(), 2e19);
    EXPECT_NEAR(ts.avg<float>(), 2e19, 1e11);
  }

  {
    // Test doubles where the sum adds up larger than a uint64_t,
    // but the average fits in an int64_t
    BucketedTimeSeries<double> ts(60, seconds(600));
    uint64_t value = 0x3fffffffffffffff;
    FOR_EACH_RANGE(i, 0, 16) {
      ts.addValue(seconds(0), value);
    }

    EXPECT_DOUBLE_EQ(value, ts.avg());
    EXPECT_DOUBLE_EQ(value, ts.avg<float>());
    // Some precision is lost here due to the huge sum, so the
    // integer average returned is off by one.
    EXPECT_NEAR(value, ts.avg<uint64_t>(), 1);
    EXPECT_NEAR(value, ts.avg<int64_t>(), 1);
  }

  {
    // Test BucketedTimeSeries with a smaller integer type
    BucketedTimeSeries<int16_t> ts(60, seconds(600));
    FOR_EACH_RANGE(i, 0, 101) {
      ts.addValue(seconds(0), i);
    }

    EXPECT_DOUBLE_EQ(50.0, ts.avg());
    EXPECT_DOUBLE_EQ(50.0, ts.avg<float>());
    EXPECT_EQ(50, ts.avg<uint64_t>());
    EXPECT_EQ(50, ts.avg<int64_t>());
    EXPECT_EQ(50, ts.avg<int16_t>());
    EXPECT_EQ(50, ts.avg<int8_t>());
  }

  {
    // Test BucketedTimeSeries with long double input
    BucketedTimeSeries<long double> ts(60, seconds(600));
    ts.addValueAggregated(seconds(0), 1000.0L, 7);

    long double expected = 1000.0L / 7.0L;
    EXPECT_DOUBLE_EQ(static_cast<double>(expected), ts.avg());
    EXPECT_DOUBLE_EQ(static_cast<float>(expected), ts.avg<float>());
    EXPECT_DOUBLE_EQ(expected, ts.avg<long double>());
    EXPECT_EQ(static_cast<uint64_t>(expected), ts.avg<uint64_t>());
    EXPECT_EQ(static_cast<int64_t>(expected), ts.avg<int64_t>());
  }

  {
    // Test BucketedTimeSeries with int64_t values,
    // but using an average that requires a fair amount of precision.
    BucketedTimeSeries<int64_t> ts(60, seconds(600));
    ts.addValueAggregated(seconds(0), 1000, 7);

    long double expected = 1000.0L / 7.0L;
    EXPECT_DOUBLE_EQ(static_cast<double>(expected), ts.avg());
    EXPECT_DOUBLE_EQ(static_cast<float>(expected), ts.avg<float>());
    EXPECT_DOUBLE_EQ(expected, ts.avg<long double>());
    EXPECT_EQ(static_cast<uint64_t>(expected), ts.avg<uint64_t>());
    EXPECT_EQ(static_cast<int64_t>(expected), ts.avg<int64_t>());
  }
}

TEST(BucketedTimeSeries, forEachBucket) {
  typedef BucketedTimeSeries<int64_t>::Bucket Bucket;
  struct BucketInfo {
    BucketInfo(const Bucket* b, seconds s, seconds ns)
      : bucket(b), start(s), nextStart(ns) {}

    const Bucket* bucket;
    seconds start;
    seconds nextStart;
  };

  for (const auto& data : testData) {
    BucketedTimeSeries<int64_t> ts(data.numBuckets, seconds(data.duration));

    vector<BucketInfo> info;
    auto fn = [&](const Bucket& bucket, seconds bucketStart,
                  seconds bucketEnd) -> bool {
      info.emplace_back(&bucket, bucketStart, bucketEnd);
      return true;
    };

    // If we haven't yet added any data, the current bucket will start at 0,
    // and all data previous buckets will have negative times.
    ts.forEachBucket(fn);

    CHECK_EQ(data.numBuckets, info.size());

    // Check the data passed in to the function
    size_t infoIdx = 0;
    size_t bucketIdx = 1;
    ssize_t offset = -data.duration;
    for (size_t n = 0; n < data.numBuckets; ++n) {
      if (bucketIdx >= data.numBuckets) {
        bucketIdx = 0;
        offset += data.duration;
      }

      EXPECT_EQ(data.bucketStarts[bucketIdx] + offset,
                info[infoIdx].start.count()) <<
        data.duration << "x" << data.numBuckets << ": bucketIdx=" <<
        bucketIdx << ", infoIdx=" << infoIdx;

      size_t nextBucketIdx = bucketIdx + 1;
      ssize_t nextOffset = offset;
      if (nextBucketIdx >= data.numBuckets) {
        nextBucketIdx = 0;
        nextOffset += data.duration;
      }
      EXPECT_EQ(data.bucketStarts[nextBucketIdx] + nextOffset,
                info[infoIdx].nextStart.count()) <<
        data.duration << "x" << data.numBuckets << ": bucketIdx=" <<
        bucketIdx << ", infoIdx=" << infoIdx;

      EXPECT_EQ(&ts.getBucketByIndex(bucketIdx), info[infoIdx].bucket);

      ++bucketIdx;
      ++infoIdx;
    }
  }
}

TEST(BucketedTimeSeries, queryByIntervalSimple) {
  BucketedTimeSeries<int> a(3, seconds(12));
  for (int i = 0; i < 8; i++) {
    a.addValue(seconds(i), 1);
  }
  // We added 1 at each second from 0..7
  // Query from the time period 0..2.
  // This is entirely in the first bucket, which has a sum of 4.
  // The code knows only part of the bucket is covered, and correctly
  // estimates the desired sum as 3.
  EXPECT_EQ(2, a.sum(seconds(0), seconds(2)));
}

TEST(BucketedTimeSeries, queryByInterval) {
  // Set up a BucketedTimeSeries tracking 6 seconds in 3 buckets
  const int kNumBuckets = 3;
  const int kDuration = 6;
  BucketedTimeSeries<double> b(kNumBuckets, seconds(kDuration));

  for (unsigned int i = 0; i < kDuration; ++i) {
    // add value 'i' at time 'i'
    b.addValue(seconds(i), i);
  }

  // Current bucket state:
  // 0: time=[0, 2): values=(0, 1), sum=1, count=2
  // 1: time=[2, 4): values=(2, 3), sum=5, count=1
  // 2: time=[4, 6): values=(4, 5), sum=9, count=2
  double expectedSums1[kDuration + 1][kDuration + 1] = {
    {0,  4.5,   9, 11.5,  14, 14.5,  15},
    {0,  4.5,   7,  9.5,  10, 10.5,  -1},
    {0,  2.5,   5,  5.5,   6,   -1,  -1},
    {0,  2.5,   3,  3.5,  -1,   -1,  -1},
    {0,  0.5,   1,   -1,  -1,   -1,  -1},
    {0,  0.5,  -1,   -1,  -1,   -1,  -1},
    {0,   -1,  -1,   -1,  -1,   -1,  -1}
  };
  int expectedCounts1[kDuration + 1][kDuration + 1] = {
    {0,  1,  2,  3,  4,  5,  6},
    {0,  1,  2,  3,  4,  5, -1},
    {0,  1,  2,  3,  4, -1, -1},
    {0,  1,  2,  3, -1, -1, -1},
    {0,  1,  2, -1, -1, -1, -1},
    {0,  1, -1, -1, -1, -1, -1},
    {0, -1, -1, -1, -1, -1, -1}
  };

  seconds currentTime = b.getLatestTime() + seconds(1);
  for (int i = 0; i <= kDuration + 1; i++) {
    for (int j = 0; j <= kDuration - i; j++) {
      seconds start = currentTime - seconds(i + j);
      seconds end = currentTime - seconds(i);
      double expectedSum = expectedSums1[i][j];
      EXPECT_EQ(expectedSum, b.sum(start, end)) <<
        "i=" << i << ", j=" << j <<
        ", interval=[" << start.count() << ", " << end.count() << ")";

      uint64_t expectedCount = expectedCounts1[i][j];
      EXPECT_EQ(expectedCount, b.count(start, end)) <<
        "i=" << i << ", j=" << j <<
        ", interval=[" << start.count() << ", " << end.count() << ")";

      double expectedAvg = expectedCount ? expectedSum / expectedCount : 0;
      EXPECT_EQ(expectedAvg, b.avg(start, end)) <<
        "i=" << i << ", j=" << j <<
        ", interval=[" << start.count() << ", " << end.count() << ")";

      double expectedRate = j ? expectedSum / j : 0;
      EXPECT_EQ(expectedRate, b.rate(start, end)) <<
        "i=" << i << ", j=" << j <<
        ", interval=[" << start.count() << ", " << end.count() << ")";
    }
  }

  // Add 3 more values.
  // This will overwrite 1 full bucket, and put us halfway through the next.
  for (unsigned int i = kDuration; i < kDuration + 3; ++i) {
    b.addValue(seconds(i), i);
  }

  // Current bucket state:
  // 0: time=[6,  8): values=(6, 7), sum=13, count=2
  // 1: time=[8, 10): values=(8),    sum=8, count=1
  // 2: time=[4,  6): values=(4, 5), sum=9, count=2
  double expectedSums2[kDuration + 1][kDuration + 1] = {
    {0,    8, 14.5,   21, 25.5,  30,  30},
    {0,  6.5,   13, 17.5,   22,  22,  -1},
    {0,  6.5,   11, 15.5, 15.5,  -1,  -1},
    {0,  4.5,    9,    9,   -1,  -1,  -1},
    {0,  4.5,  4.5,   -1,   -1,  -1,  -1},
    {0,    0,   -1,   -1,   -1,  -1,  -1},
    {0,   -1,   -1,   -1,   -1,  -1,  -1}
  };
  int expectedCounts2[kDuration + 1][kDuration + 1] = {
    {0,  1,  2,  3,  4,  5,  5},
    {0,  1,  2,  3,  4,  4, -1},
    {0,  1,  2,  3,  3, -1, -1},
    {0,  1,  2,  2, -1, -1, -1},
    {0,  1,  1, -1, -1, -1, -1},
    {0,  0, -1, -1, -1, -1, -1},
    {0, -1, -1, -1, -1, -1, -1}
  };

  currentTime = b.getLatestTime() + seconds(1);
  for (int i = 0; i <= kDuration + 1; i++) {
    for (int j = 0; j <= kDuration - i; j++) {
      seconds start = currentTime - seconds(i + j);
      seconds end = currentTime - seconds(i);
      double expectedSum = expectedSums2[i][j];
      EXPECT_EQ(expectedSum, b.sum(start, end)) <<
        "i=" << i << ", j=" << j <<
        ", interval=[" << start.count() << ", " << end.count() << ")";

      uint64_t expectedCount = expectedCounts2[i][j];
      EXPECT_EQ(expectedCount, b.count(start, end)) <<
        "i=" << i << ", j=" << j <<
        ", interval=[" << start.count() << ", " << end.count() << ")";

      double expectedAvg = expectedCount ? expectedSum / expectedCount : 0;
      EXPECT_EQ(expectedAvg, b.avg(start, end)) <<
        "i=" << i << ", j=" << j <<
        ", interval=[" << start.count() << ", " << end.count() << ")";

      double expectedRate = j ? expectedSum / j : 0;
      EXPECT_EQ(expectedRate, b.rate(start, end)) <<
        "i=" << i << ", j=" << j <<
        ", interval=[" << start.count() << ", " << end.count() << ")";
    }
  }
}
