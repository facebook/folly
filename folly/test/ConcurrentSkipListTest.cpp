/*
 * Copyright 2012 Facebook, Inc.
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

// @author: Xin Liu <xliux@fb.com>

#include <set>
#include <vector>
#include <boost/thread.hpp>

#include <glog/logging.h>
#include <gflags/gflags.h>
#include "folly/ConcurrentSkipList.h"
#include "folly/Foreach.h"
#include "gtest/gtest.h"

DEFINE_int32(num_threads, 12, "num concurrent threads to test");

namespace {

using namespace folly;
using std::vector;

typedef int ValueType;
typedef detail::SkipListNode<ValueType> SkipListNodeType;
typedef ConcurrentSkipList<ValueType> SkipListType;
typedef SkipListType::Accessor SkipListAccessor;
typedef vector<ValueType> VectorType;
typedef std::set<ValueType> SetType;

static const int kHeadHeight = 2;
static const int kMaxValue = 5000;

static void randomAdding(int size,
    SkipListAccessor skipList,
    SetType *verifier,
    int maxValue = kMaxValue) {
  for (int i = 0; i < size; ++i) {
    int32_t r = rand() % maxValue;
    verifier->insert(r);
    skipList.add(r);
  }
}

static void randomRemoval(int size,
    SkipListAccessor skipList,
    SetType *verifier,
    int maxValue=kMaxValue) {
  for (int i = 0; i < size; ++i) {
    int32_t r = rand() % maxValue;
    verifier->insert(r);
    skipList.remove(r);
  }
}

static void sumAllValues(SkipListAccessor skipList, int64_t *sum) {
  *sum = 0;
  FOR_EACH(it, skipList) {
    *sum += *it;
  }
  VLOG(20) << "sum = " << sum;
}

static void concurrentSkip(const vector<ValueType> *values,
    SkipListAccessor skipList) {
  int64_t sum = 0;
  SkipListAccessor::Skipper skipper(skipList);
  FOR_EACH(it, *values) {
    if (skipper.to(*it)) sum += *it;
  }
  VLOG(20) << "sum = " << sum;
}

bool verifyEqual(SkipListAccessor skipList,
    const SetType &verifier) {
  EXPECT_EQ(verifier.size(), skipList.size());
  FOR_EACH(it, verifier) {
    CHECK(skipList.contains(*it)) << *it;
    SkipListType::const_iterator iter = skipList.find(*it);
    CHECK(iter != skipList.end());
    EXPECT_EQ(*iter, *it);
  }
  EXPECT_TRUE(std::equal(verifier.begin(), verifier.end(), skipList.begin()));
  return true;
}

TEST(ConcurrentSkipList, SequentialAccess) {
  {
    LOG(INFO) << "nodetype size=" << sizeof(SkipListNodeType);

    auto skipList(SkipListType::create(kHeadHeight));
    EXPECT_TRUE(skipList.first() == NULL);
    EXPECT_TRUE(skipList.last() == NULL);

    skipList.add(3);
    EXPECT_TRUE(skipList.contains(3));
    EXPECT_FALSE(skipList.contains(2));
    EXPECT_EQ(3, *skipList.first());
    EXPECT_EQ(3, *skipList.last());

    EXPECT_EQ(3, *skipList.find(3));
    EXPECT_FALSE(skipList.find(3) == skipList.end());
    EXPECT_TRUE(skipList.find(2) == skipList.end());

    {
      SkipListAccessor::Skipper skipper(skipList);
      skipper.to(3);
      CHECK_EQ(3, *skipper);
    }

    skipList.add(2);
    EXPECT_EQ(2, *skipList.first());
    EXPECT_EQ(3, *skipList.last());
    skipList.add(5);
    EXPECT_EQ(5, *skipList.last());
    skipList.add(3);
    EXPECT_EQ(5, *skipList.last());
    auto ret = skipList.insert(9);
    EXPECT_EQ(9, *ret.first);
    EXPECT_TRUE(ret.second);

    ret = skipList.insert(5);
    EXPECT_EQ(5, *ret.first);
    EXPECT_FALSE(ret.second);

    EXPECT_EQ(2, *skipList.first());
    EXPECT_EQ(9, *skipList.last());
    EXPECT_TRUE(skipList.pop_back());
    EXPECT_EQ(5, *skipList.last());
    EXPECT_TRUE(skipList.pop_back());
    EXPECT_EQ(3, *skipList.last());

    skipList.add(9);
    skipList.add(5);

    CHECK(skipList.contains(2));
    CHECK(skipList.contains(3));
    CHECK(skipList.contains(5));
    CHECK(skipList.contains(9));
    CHECK(!skipList.contains(4));

    // lower_bound
    auto it = skipList.lower_bound(5);
    EXPECT_EQ(5, *it);
    it = skipList.lower_bound(4);
    EXPECT_EQ(5, *it);
    it = skipList.lower_bound(9);
    EXPECT_EQ(9, *it);
    it = skipList.lower_bound(12);
    EXPECT_FALSE(it.good());

    it = skipList.begin();
    EXPECT_EQ(2, *it);

    // skipper test
    SkipListAccessor::Skipper skipper(skipList);
    skipper.to(3);
    EXPECT_EQ(3, skipper.data());
    skipper.to(5);
    EXPECT_EQ(5, skipper.data());
    CHECK(!skipper.to(7));

    skipList.remove(5);
    skipList.remove(3);
    CHECK(skipper.to(9));
    EXPECT_EQ(9, skipper.data());

    CHECK(!skipList.contains(3));
    skipList.add(3);
    CHECK(skipList.contains(3));
    int pos = 0;
    FOR_EACH(it, skipList) {
      LOG(INFO) << "pos= " << pos++ << " value= " << *it;
    }
  }

  {
    auto skipList(SkipListType::create(kHeadHeight));

    SetType verifier;
    randomAdding(10000, skipList, &verifier);
    verifyEqual(skipList, verifier);

    // test skipper
    SkipListAccessor::Skipper skipper(skipList);
    int num_skips = 1000;
    for (int i = 0; i < num_skips; ++i) {
      int n = i * kMaxValue / num_skips;
      bool found = skipper.to(n);
      EXPECT_EQ(found, (verifier.find(n) != verifier.end()));
    }
  }

}

void testConcurrentAdd(int numThreads) {
  auto skipList(SkipListType::create(kHeadHeight));

  vector<boost::thread> threads;
  vector<SetType> verifiers(numThreads);
  for (int i = 0; i < numThreads; ++i) {
    threads.push_back(boost::thread(
          &randomAdding, 100, skipList, &verifiers[i], kMaxValue));
  }
  for (int i = 0; i < threads.size(); ++i) {
    threads[i].join();
  }

  SetType all;
  FOR_EACH(s, verifiers) {
    all.insert(s->begin(), s->end());
  }
  verifyEqual(skipList, all);
}

TEST(ConcurrentSkipList, ConcurrentAdd) {
  // test it many times
  for (int numThreads = 10; numThreads < 10000; numThreads += 1000) {
    testConcurrentAdd(numThreads);
  }
}

void testConcurrentRemoval(int numThreads, int maxValue) {
  auto skipList = SkipListType::create(kHeadHeight);
  for (int i = 0; i < maxValue; ++i) {
    skipList.add(i);
  }

  vector<boost::thread> threads;
  vector<SetType > verifiers(numThreads);
  for (int i = 0; i < numThreads; ++i) {
    threads.push_back(boost::thread(
          &randomRemoval, 100, skipList, &verifiers[i], maxValue));
  }
  FOR_EACH(t, threads) {
    (*t).join();
  }

  SetType all;
  FOR_EACH(s, verifiers) {
    all.insert(s->begin(), s->end());
  }

  CHECK_EQ(maxValue, all.size() + skipList.size());
  for (int i = 0; i < maxValue; ++i) {
    if (all.find(i) != all.end()) {
      CHECK(!skipList.contains(i)) << i;
    } else {
      CHECK(skipList.contains(i)) << i;
    }
  }
}

TEST(ConcurrentSkipList, ConcurrentRemove) {
  for (int numThreads = 10; numThreads < 1000; numThreads += 100) {
    testConcurrentRemoval(numThreads, 100 * numThreads);
  }
}

static void testConcurrentAccess(
    int numInsertions, int numDeletions, int maxValue) {
  auto skipList = SkipListType::create(kHeadHeight);

  vector<SetType> verifiers(FLAGS_num_threads);
  vector<int64_t> sums(FLAGS_num_threads);
  vector<vector<ValueType> > skipValues(FLAGS_num_threads);

  for (int i = 0; i < FLAGS_num_threads; ++i) {
    for (int j = 0; j < numInsertions; ++j) {
      skipValues[i].push_back(rand() % (maxValue + 1));
    }
    std::sort(skipValues[i].begin(), skipValues[i].end());
  }

  vector<boost::thread> threads;
  for (int i = 0; i < FLAGS_num_threads; ++i) {
    switch (i % 8) {
      case 0:
      case 1:
        threads.push_back(boost::thread(
              randomAdding, numInsertions, skipList, &verifiers[i], maxValue));
        break;
      case 2:
        threads.push_back(boost::thread(
              randomRemoval, numDeletions, skipList, &verifiers[i], maxValue));
        break;
      case 3:
        threads.push_back(boost::thread(
              concurrentSkip, &skipValues[i], skipList));
        break;
      default:
        threads.push_back(boost::thread(sumAllValues, skipList, &sums[i]));
        break;
    }
  }

  FOR_EACH(t, threads) {
    (*t).join();
  }
  // just run through it, no need to verify the correctness.
}

TEST(ConcurrentSkipList, ConcurrentAccess) {
  testConcurrentAccess(10000, 100, kMaxValue);
  testConcurrentAccess(100000, 10000, kMaxValue * 10);
  testConcurrentAccess(1000000, 100000, kMaxValue);
}

}  // namespace

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  return RUN_ALL_TESTS();
}
