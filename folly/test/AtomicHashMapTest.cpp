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

#include "folly/AtomicHashMap.h"

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <sys/time.h>
#include <thread>
#include <atomic>

#include "folly/Benchmark.h"
#include "folly/Conv.h"

using std::vector;
using std::string;
using folly::AtomicHashMap;
using folly::AtomicHashArray;

// Tunables:
DEFINE_double(targetLoadFactor, 0.75, "Target memory utilization fraction.");
DEFINE_double(maxLoadFactor, 0.80, "Max before growth.");
DEFINE_int32(numThreads, 8, "Threads to use for concurrency tests.");
DEFINE_int64(numBMElements, 12 * 1000 * 1000, "Size of maps for benchmarks.");

const double LF = FLAGS_maxLoadFactor / FLAGS_targetLoadFactor;
const int maxBMElements = int(FLAGS_numBMElements * LF); // hit our target LF.

static int64_t nowInUsec() {
  timeval tv;
  gettimeofday(&tv, 0);
  return int64_t(tv.tv_sec) * 1000 * 1000 + tv.tv_usec;
}

TEST(Ahm, BasicStrings) {
  typedef AtomicHashMap<int64_t,string> AHM;
  AHM myMap(1024);
  EXPECT_TRUE(myMap.begin() == myMap.end());

  for (int i = 0; i < 100; ++i) {
    myMap.insert(make_pair(i, folly::to<string>(i)));
  }
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(myMap.find(i)->second, folly::to<string>(i));
  }

  myMap.insert(std::make_pair(999, "A"));
  myMap.insert(std::make_pair(999, "B"));
  EXPECT_EQ(myMap.find(999)->second, "A"); // shouldn't have overwritten
  myMap.find(999)->second = "B";
  myMap.find(999)->second = "C";
  EXPECT_EQ(myMap.find(999)->second, "C");
  EXPECT_EQ(myMap.find(999)->first, 999);
}

typedef int32_t     KeyT;
typedef int64_t     KeyTBig;
typedef int32_t     ValueT;

typedef AtomicHashMap<KeyT,ValueT> AHMapT;
typedef AHMapT::value_type RecordT;
typedef AtomicHashArray<KeyT,ValueT> AHArrayT;

AHArrayT::Config config;
static AHArrayT::SmartPtr globalAHA(nullptr);
static std::unique_ptr<AHMapT> globalAHM;

// Generate a deterministic value based on an input key
static int genVal(int key) {
  return key / 3;
}

TEST(Ahm, grow) {
  VLOG(1) << "Overhead: " << sizeof(AHArrayT) << " (array) " <<
    sizeof(AHMapT) + sizeof(AHArrayT) << " (map/set) Bytes.";
  int numEntries = 10000;
  float sizeFactor = 0.46;

  std::unique_ptr<AHMapT> m(new AHMapT(int(numEntries * sizeFactor), config));

  // load map - make sure we succeed and the index is accurate
  bool success = true;
  for (uint64_t i = 0; i < numEntries; i++) {
    auto ret = m->insert(RecordT(i, genVal(i)));
    success &= ret.second;
    success &= (m->findAt(ret.first.getIndex())->second == genVal(i));
  }
  // Overwrite vals to make sure there are no dups
  // Every insert should fail because the keys are already in the map.
  success = true;
  for (uint64_t i = 0; i < numEntries; i++) {
    auto ret = m->insert(RecordT(i, genVal(i * 2)));
    success &= (ret.second == false);  // fail on collision
    success &= (ret.first->second == genVal(i)); // return the previous value
    success &= (m->findAt(ret.first.getIndex())->second == genVal(i));
  }
  EXPECT_TRUE(success);

  // check correctness
  size_t cap = m->capacity();
  ValueT val;
  EXPECT_GT(m->numSubMaps(), 1);  // make sure we grew
  success = true;
  EXPECT_EQ(m->size(), numEntries);
  for (int i = 0; i < numEntries; i++) {
    success &= (m->find(i)->second == genVal(i));
  }
  EXPECT_TRUE(success);

  // Check findAt
  success = true;
  KeyT key(0);
  AHMapT::const_iterator retIt;
  for (uint64_t i = 0; i < numEntries; i++) {
    retIt = m->find(i);
    retIt = m->findAt(retIt.getIndex());
    success &= (retIt->second == genVal(i));
    success &= (retIt->first == i);
  }
  EXPECT_TRUE(success);

  // Try modifying value
  m->find(8)->second = 5309;
  EXPECT_EQ(m->find(8)->second, 5309);

  // check clear()
  m->clear();
  success = true;
  for (uint64_t i = 0; i < numEntries / 2; i++) {
    success &= m->insert(RecordT(i, genVal(i))).second;
  }
  EXPECT_TRUE(success);
  EXPECT_EQ(m->size(), numEntries / 2);
}

TEST(Ahm, iterator) {
  int numEntries = 10000;
  float sizeFactor = .46;
  std::unique_ptr<AHMapT> m(new AHMapT(int(numEntries * sizeFactor), config));

  // load map - make sure we succeed and the index is accurate
  for (uint64_t i = 0; i < numEntries; i++) {
    m->insert(RecordT(i, genVal(i)));
  }

  bool success = true;
  int count = 0;
  FOR_EACH(it, *m) {
    success &= (it->second == genVal(it->first));
    ++count;
  }
  EXPECT_TRUE(success);
  EXPECT_EQ(count, numEntries);
}

class Counters {
private:
  // NOTE: Unfortunately can't currently put a std::atomic<int64_t> in
  // the value in ahm since it doesn't support non-copyable but
  // move-constructible value types yet.
  AtomicHashMap<int64_t,int64_t> ahm;

public:
  explicit Counters(size_t numCounters) : ahm(numCounters) {}

  void increment(int64_t obj_id) {
    auto ret = ahm.insert(std::make_pair(obj_id, 1));
    if (!ret.second) {
      // obj_id already exists, increment count
      __sync_fetch_and_add(&ret.first->second, 1);
    }
  }

  int64_t getValue(int64_t obj_id) {
    auto ret = ahm.find(obj_id);
    return ret != ahm.end() ? ret->second : 0;
  }

  // export the counters without blocking increments
  string toString() {
    string ret = "{\n";
    ret.reserve(ahm.size() * 32);
    for (const auto& e : ahm) {
      ret += folly::to<string>(
        "  [", e.first, ":", e.second, "]\n");
    }
    ret += "}\n";
    return ret;
  }
};

// If you get an error "terminate called without an active exception", there
// might be too many threads getting created - decrease numKeys and/or mult.
TEST(Ahm, counter) {
  const int numKeys = 10;
  const int mult = 10;
  Counters c(numKeys);
  vector<int64_t> keys;
  FOR_EACH_RANGE(i, 1, numKeys) {
    keys.push_back(i);
  }
  vector<std::thread> threads;
  for (auto key : keys) {
    FOR_EACH_RANGE(i, 0, key * mult) {
      threads.push_back(std::thread([&, key] { c.increment(key); }));
    }
  }
  for (auto& t : threads) {
    t.join();
  }
  string str = c.toString();
  for (auto key : keys) {
    int val = key * mult;
    EXPECT_EQ(val, c.getValue(key));
    EXPECT_NE(string::npos, str.find(folly::to<string>("[",key,":",val,"]")));
  }
}

class Integer {

 public:
  explicit Integer(KeyT v = 0) : v_(v) {}

  Integer& operator=(const Integer& a) {
    static bool throwException_ = false;
    throwException_ = !throwException_;
    if (throwException_) {
      throw 1;
    }
    v_ = a.v_;
    return *this;
  }

  bool operator==(const Integer& a) const { return v_ == a.v_; }

 private:
  KeyT v_;
};

TEST(Ahm, map_exception_safety) {
  typedef AtomicHashMap<KeyT,Integer> MyMapT;

  int numEntries = 10000;
  float sizeFactor = 0.46;
  std::unique_ptr<MyMapT> m(new MyMapT(int(numEntries * sizeFactor)));

  bool success = true;
  int count = 0;
  for (int i = 0; i < numEntries; i++) {
    try {
      m->insert(i, Integer(genVal(i)));
      success &= (m->find(i)->second == Integer(genVal(i)));
      ++count;
    } catch (...) {
      success &= !m->count(i);
    }
  }
  EXPECT_EQ(count, m->size());
  EXPECT_TRUE(success);
}

TEST(Ahm, basicErase) {
  int numEntries = 3000;

  std::unique_ptr<AHMapT> s(new AHMapT(numEntries, config));
  // Iterate filling up the map and deleting all keys a few times
  // to test more than one subMap.
  for (int iterations = 0; iterations < 4; ++iterations) {
    // Testing insertion of keys
    bool success = true;
    for (uint64_t i = 0; i < numEntries; ++i) {
      success &= !(s->count(i));
      auto ret = s->insert(RecordT(i, i));
      success &= s->count(i);
      success &= ret.second;
    }
    EXPECT_TRUE(success);
    EXPECT_EQ(s->size(), numEntries);

    // Delete every key in the map and verify that the key is gone and the the
    // size is correct.
    success = true;
    for (uint64_t i = 0; i < numEntries; ++i) {
      success &= s->erase(i);
      success &= (s->size() == numEntries - 1 - i);
      success &= !(s->count(i));
      success &= !(s->erase(i));
    }
    EXPECT_TRUE(success);
  }
  VLOG(1) << "Final number of subMaps = " << s->numSubMaps();
}

namespace {

inline KeyT randomizeKey(int key) {
  // We deterministically randomize the key to more accurately simulate
  // real-world usage, and to avoid pathalogical performance patterns (e.g.
  // those related to __gnu_cxx::hash<int64_t>()(1) == 1).
  //
  // Use a hash function we don't normally use for ints to avoid interactions.
  return folly::hash::jenkins_rev_mix32(key);
}

int numOpsPerThread = 0;

void* insertThread(void* jj) {
  int64_t j = (int64_t) jj;
  for (int i = 0; i < numOpsPerThread; ++i) {
    KeyT key = randomizeKey(i + j * numOpsPerThread);
    globalAHM->insert(key, genVal(key));
  }
  return NULL;
}

void* insertThreadArr(void* jj) {
  int64_t j = (int64_t) jj;
  for (int i = 0; i < numOpsPerThread; ++i) {
    KeyT key = randomizeKey(i + j * numOpsPerThread);
    globalAHA->insert(std::make_pair(key, genVal(key)));
  }
  return NULL;
}

std::atomic<bool> runThreadsCreatedAllThreads;
void runThreads(void *(*thread)(void*), int numThreads, void **statuses) {
  folly::BenchmarkSuspender susp;
  runThreadsCreatedAllThreads.store(false);
  vector<pthread_t> threadIds;
  for (int64_t j = 0; j < numThreads; j++) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, thread, (void*) j) != 0) {
       LOG(ERROR) << "Could not start thread";
    } else {
      threadIds.push_back(tid);
    }
  }
  susp.dismiss();

  runThreadsCreatedAllThreads.store(true);
  for (int i = 0; i < threadIds.size(); ++i) {
    pthread_join(threadIds[i], statuses == NULL ? NULL : &statuses[i]);
  }
}

void runThreads(void *(*thread)(void*)) {
  runThreads(thread, FLAGS_numThreads, NULL);
}

}

TEST(Ahm, collision_test) {
  const int numInserts = 1000000 / 4;

  // Doing the same number on each thread so we collide.
  numOpsPerThread = numInserts;

  float sizeFactor = 0.46;
  int entrySize = sizeof(KeyT) + sizeof(ValueT);
  VLOG(1) << "Testing " << numInserts << " unique " << entrySize <<
    " Byte entries replicated in " << FLAGS_numThreads <<
    " threads with " << FLAGS_maxLoadFactor * 100.0 << "% max load factor.";

  globalAHM.reset(new AHMapT(int(numInserts * sizeFactor), config));

  size_t sizeInit = globalAHM->capacity();
  VLOG(1) << "  Initial capacity: " << sizeInit;

  double start = nowInUsec();
  runThreads([](void*) -> void* { // collisionInsertThread
    for (int i = 0; i < numOpsPerThread; ++i) {
      KeyT key = randomizeKey(i);
      globalAHM->insert(key, genVal(key));
    }
    return nullptr;
  });
  double elapsed = nowInUsec() - start;

  size_t finalCap = globalAHM->capacity();
  size_t sizeAHM = globalAHM->size();
  VLOG(1) << elapsed/sizeAHM << " usec per " << FLAGS_numThreads <<
    " duplicate inserts (atomic).";
  VLOG(1) << "  Final capacity: " << finalCap << " in " <<
    globalAHM->numSubMaps() << " sub maps (" <<
    sizeAHM * 100 / finalCap << "% load factor, " <<
    (finalCap - sizeInit) * 100 / sizeInit << "% growth).";

  // check correctness
  EXPECT_EQ(sizeAHM, numInserts);
  bool success = true;
  ValueT val;
  for (int i = 0; i < numInserts; ++i) {
    KeyT key = randomizeKey(i);
    success &= (globalAHM->find(key)->second == genVal(key));
  }
  EXPECT_TRUE(success);

  // check colliding finds
  start = nowInUsec();
  runThreads([](void*) -> void* { // collisionFindThread
    KeyT key(0);
    for (int i = 0; i < numOpsPerThread; ++i) {
      globalAHM->find(key);
    }
    return nullptr;
  });

  elapsed = nowInUsec() - start;

  VLOG(1) << elapsed/sizeAHM << " usec per " << FLAGS_numThreads <<
    " duplicate finds (atomic).";
}

namespace {

const int kInsertPerThread = 100000;
int raceFinalSizeEstimate;

void* raceIterateThread(void* jj) {
  int64_t j = (int64_t) jj;
  int count = 0;

  AHMapT::iterator it = globalAHM->begin();
  AHMapT::iterator end = globalAHM->end();
  for (; it != end; ++it) {
    ++count;
    if (count > raceFinalSizeEstimate) {
      EXPECT_FALSE("Infinite loop in iterator.");
      return NULL;
    }
  }
  return NULL;
}

void* raceInsertRandomThread(void* jj) {
  int64_t j = (int64_t) jj;
  for (int i = 0; i < kInsertPerThread; ++i) {
    KeyT key = rand();
    globalAHM->insert(key, genVal(key));
  }
  return NULL;
}

}

// Test for race conditions when inserting and iterating at the same time and
// creating multiple submaps.
TEST(Ahm, race_insert_iterate_thread_test) {
  const int kInsertThreads = 20;
  const int kIterateThreads = 20;
  raceFinalSizeEstimate = kInsertThreads * kInsertPerThread;

  VLOG(1) << "Testing iteration and insertion with " << kInsertThreads
    << " threads inserting and " << kIterateThreads << " threads iterating.";

  globalAHM.reset(new AHMapT(raceFinalSizeEstimate / 9, config));

  vector<pthread_t> threadIds;
  for (int64_t j = 0; j < kInsertThreads + kIterateThreads; j++) {
    pthread_t tid;
    void *(*thread)(void*) =
      (j < kInsertThreads ? raceInsertRandomThread : raceIterateThread);
    if (pthread_create(&tid, NULL, thread, (void*) j) != 0) {
      LOG(ERROR) << "Could not start thread";
    } else {
      threadIds.push_back(tid);
    }
  }
  for (int i = 0; i < threadIds.size(); ++i) {
    pthread_join(threadIds[i], NULL);
  }
  VLOG(1) << "Ended up with " << globalAHM->numSubMaps() << " submaps";
  VLOG(1) << "Final size of map " << globalAHM->size();
}

namespace {

const int kTestEraseInsertions = 200000;
std::atomic<int32_t> insertedLevel;

void* testEraseInsertThread(void*) {
  for (int i = 0; i < kTestEraseInsertions; ++i) {
    KeyT key = randomizeKey(i);
    globalAHM->insert(key, genVal(key));
    insertedLevel.store(i, std::memory_order_release);
  }
  insertedLevel.store(kTestEraseInsertions, std::memory_order_release);
  return NULL;
}

void* testEraseEraseThread(void*) {
  for (int i = 0; i < kTestEraseInsertions; ++i) {
    /*
     * Make sure that we don't get ahead of the insert thread, because
     * part of the condition for this unit test succeeding is that the
     * map ends up empty.
     *
     * Note, there is a subtle case here when a new submap is
     * allocated: the erasing thread might get 0 from count(key)
     * because it hasn't seen numSubMaps_ update yet.  To avoid this
     * race causing problems for the test (it's ok for real usage), we
     * lag behind the inserter by more than just element.
     */
    const int lag = 10;
    int currentLevel;
    do {
      currentLevel = insertedLevel.load(std::memory_order_acquire);
      if (currentLevel == kTestEraseInsertions) currentLevel += lag + 1;
    } while (currentLevel - lag < i);

    KeyT key = randomizeKey(i);
    while (globalAHM->count(key)) {
      if (globalAHM->erase(key)) {
        break;
      }
    }
  }
  return NULL;
}

}

// Here we have a single thread inserting some values, and several threads
// racing to delete the values in the order they were inserted.
TEST(Ahm, thread_erase_insert_race) {
  const int kInsertThreads = 1;
  const int kEraseThreads = 10;

  VLOG(1) << "Testing insertion and erase with " << kInsertThreads
    << " thread inserting and " << kEraseThreads << " threads erasing.";

  globalAHM.reset(new AHMapT(kTestEraseInsertions / 4, config));

  vector<pthread_t> threadIds;
  for (int64_t j = 0; j < kInsertThreads + kEraseThreads; j++) {
    pthread_t tid;
    void *(*thread)(void*) =
      (j < kInsertThreads ? testEraseInsertThread : testEraseEraseThread);
    if (pthread_create(&tid, NULL, thread, (void*) j) != 0) {
      LOG(ERROR) << "Could not start thread";
    } else {
      threadIds.push_back(tid);
    }
  }
  for (int i = 0; i < threadIds.size(); i++) {
    pthread_join(threadIds[i], NULL);
  }

  EXPECT_TRUE(globalAHM->empty());
  EXPECT_EQ(globalAHM->size(), 0);

  VLOG(1) << "Ended up with " << globalAHM->numSubMaps() << " submaps";
}

// Repro for T#483734: Duplicate AHM inserts due to incorrect AHA return value.
typedef AtomicHashArray<int32_t, int32_t> AHA;
AHA::Config configRace;
auto atomicHashArrayInsertRaceArray = AHA::create(2, configRace);
void* atomicHashArrayInsertRaceThread(void* j) {
  AHA* arr = atomicHashArrayInsertRaceArray.get();
  uintptr_t numInserted = 0;
  while (!runThreadsCreatedAllThreads.load());
  for (int i = 0; i < 2; i++) {
    if (arr->insert(RecordT(randomizeKey(i), 0)).first != arr->end()) {
      numInserted++;
    }
  }
  pthread_exit((void *) numInserted);
}
TEST(Ahm, atomic_hash_array_insert_race) {
  AHA* arr = atomicHashArrayInsertRaceArray.get();
  int numIterations = 50000, FLAGS_numThreads = 4;
  void* statuses[FLAGS_numThreads];
  for (int i = 0; i < numIterations; i++) {
    arr->clear();
    runThreads(atomicHashArrayInsertRaceThread, FLAGS_numThreads, statuses);
    EXPECT_GE(arr->size(), 1);
    for (int j = 0; j < FLAGS_numThreads; j++) {
      EXPECT_EQ(arr->size(), uintptr_t(statuses[j]));
    }
  }
}

namespace {

void loadGlobalAha() {
  std::cout << "loading global AHA with " << FLAGS_numThreads
            << " threads...\n";
  uint64_t start = nowInUsec();
  globalAHA = AHArrayT::create(maxBMElements, config);
  numOpsPerThread = FLAGS_numBMElements / FLAGS_numThreads;
  CHECK_EQ(0, FLAGS_numBMElements % FLAGS_numThreads) <<
    "kNumThreads must evenly divide kNumInserts.";
  runThreads(insertThreadArr);
  uint64_t elapsed = nowInUsec() - start;
  std::cout << "  took " << elapsed / 1000 << " ms (" <<
    (elapsed * 1000 / FLAGS_numBMElements) << " ns/insert).\n";
  EXPECT_EQ(globalAHA->size(), FLAGS_numBMElements);
}

void loadGlobalAhm() {
  std::cout << "loading global AHM with " << FLAGS_numThreads
            << " threads...\n";
  uint64_t start = nowInUsec();
  globalAHM.reset(new AHMapT(maxBMElements, config));
  numOpsPerThread = FLAGS_numBMElements / FLAGS_numThreads;
  runThreads(insertThread);
  uint64_t elapsed = nowInUsec() - start;
  std::cout << "  took " << elapsed / 1000 << " ms (" <<
    (elapsed * 1000 / FLAGS_numBMElements) << " ns/insert).\n";
  EXPECT_EQ(globalAHM->size(), FLAGS_numBMElements);
}

}

BENCHMARK(st_aha_find, iters) {
  CHECK_LE(iters, FLAGS_numBMElements);
  for (int i = 0; i < iters; i++) {
    KeyT key = randomizeKey(i);
    folly::doNotOptimizeAway(globalAHA->find(key)->second);
  }
}

BENCHMARK(st_ahm_find, iters) {
  CHECK_LE(iters, FLAGS_numBMElements);
  for (int i = 0; i < iters; i++) {
    KeyT key = randomizeKey(i);
    folly::doNotOptimizeAway(globalAHM->find(key)->second);
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK(mt_ahm_miss, iters) {
  CHECK_LE(iters, FLAGS_numBMElements);
  numOpsPerThread = iters / FLAGS_numThreads;
  runThreads([](void* jj) -> void* {
    int64_t j = (int64_t) jj;
    while (!runThreadsCreatedAllThreads.load());
    for (int i = 0; i < numOpsPerThread; ++i) {
      KeyT key = i + j * numOpsPerThread * 100;
      folly::doNotOptimizeAway(globalAHM->find(key) == globalAHM->end());
    }
    return nullptr;
  });
}

BENCHMARK(st_ahm_miss, iters) {
  CHECK_LE(iters, FLAGS_numBMElements);
  for (int i = 0; i < iters; i++) {
    KeyT key = randomizeKey(i + iters * 100);
    folly::doNotOptimizeAway(globalAHM->find(key) == globalAHM->end());
  }
}

BENCHMARK(mt_ahm_find_insert_mix, iters) {
  CHECK_LE(iters, FLAGS_numBMElements);
  numOpsPerThread = iters / FLAGS_numThreads;
  runThreads([](void* jj) -> void* {
    int64_t j = (int64_t) jj;
    while (!runThreadsCreatedAllThreads.load());
    for (int i = 0; i < numOpsPerThread; ++i) {
      if (i % 128) {  // ~1% insert mix
        KeyT key = randomizeKey(i + j * numOpsPerThread);
        folly::doNotOptimizeAway(globalAHM->find(key)->second);
      } else {
        KeyT key = randomizeKey(i + j * numOpsPerThread * 100);
        globalAHM->insert(key, genVal(key));
      }
    }
    return nullptr;
  });
}

BENCHMARK(mt_aha_find, iters) {
  CHECK_LE(iters, FLAGS_numBMElements);
  numOpsPerThread = iters / FLAGS_numThreads;
  runThreads([](void* jj) -> void* {
      int64_t j = (int64_t) jj;
      while (!runThreadsCreatedAllThreads.load());
      for (int i = 0; i < numOpsPerThread; ++i) {
        KeyT key = randomizeKey(i + j * numOpsPerThread);
        folly::doNotOptimizeAway(globalAHA->find(key)->second);
      }
      return nullptr;
    });
}

BENCHMARK(mt_ahm_find, iters) {
  CHECK_LE(iters, FLAGS_numBMElements);
  numOpsPerThread = iters / FLAGS_numThreads;
  runThreads([](void* jj) -> void* {
    int64_t j = (int64_t) jj;
    while (!runThreadsCreatedAllThreads.load());
    for (int i = 0; i < numOpsPerThread; ++i) {
      KeyT key = randomizeKey(i + j * numOpsPerThread);
      folly::doNotOptimizeAway(globalAHM->find(key)->second);
    }
    return nullptr;
  });
}

KeyT k;
BENCHMARK(st_baseline_modulus_and_random, iters) {
  for (int i = 0; i < iters; ++i) {
    k = randomizeKey(i) % iters;
  }
}

// insertions go last because they reset the map

BENCHMARK(mt_ahm_insert, iters) {
  BENCHMARK_SUSPEND {
    globalAHM.reset(new AHMapT(int(iters * LF), config));
    numOpsPerThread = iters / FLAGS_numThreads;
  }
  runThreads(insertThread);
}

BENCHMARK(st_ahm_insert, iters) {
  folly::BenchmarkSuspender susp;
  std::unique_ptr<AHMapT> ahm(new AHMapT(int(iters * LF), config));
  susp.dismiss();

  for (int i = 0; i < iters; i++) {
    KeyT key = randomizeKey(i);
    ahm->insert(key, genVal(key));
  }
}

void benchmarkSetup() {
  config.maxLoadFactor = FLAGS_maxLoadFactor;
  configRace.maxLoadFactor = 0.5;
  int numCores = sysconf(_SC_NPROCESSORS_ONLN);
  loadGlobalAha();
  loadGlobalAhm();
  string numIters = folly::to<string>(
    std::min(1000000, int(FLAGS_numBMElements)));

  google::SetCommandLineOptionWithMode(
    "bm_max_iters", numIters.c_str(), google::SET_FLAG_IF_DEFAULT
  );
  google::SetCommandLineOptionWithMode(
    "bm_min_iters", numIters.c_str(), google::SET_FLAG_IF_DEFAULT
  );
  string numCoresStr = folly::to<string>(numCores);
  google::SetCommandLineOptionWithMode(
    "numThreads", numCoresStr.c_str(), google::SET_FLAG_IF_DEFAULT
  );

  std::cout << "\nRunning AHM benchmarks on machine with " << numCores
    << " logical cores.\n"
       "  num elements per map: " << FLAGS_numBMElements << "\n"
    << "  num threads for mt tests: " << FLAGS_numThreads << "\n"
    << "  AHM load factor: " << FLAGS_targetLoadFactor << "\n\n";
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  auto ret = RUN_ALL_TESTS();
  if (!ret && FLAGS_benchmark) {
    benchmarkSetup();
    folly::runBenchmarks();
  }
  return ret;
}

/*
Benchmarks run on dual Xeon X5650's @ 2.67GHz w/hyperthreading enabled
  (12 physical cores, 12 MB cache, 72 GB RAM)

Running AHM benchmarks on machine with 24 logical cores.
  num elements per map: 12000000
  num threads for mt tests: 24
  AHM load factor: 0.75

Benchmark                               Iters   Total t    t/iter iter/sec
------------------------------------------------------------------------------
Comparing benchmarks: BM_mt_aha_find,BM_mt_ahm_find
*       BM_mt_aha_find                1000000  7.767 ms  7.767 ns  122.8 M
 +0.81% BM_mt_ahm_find                1000000   7.83 ms   7.83 ns  121.8 M
------------------------------------------------------------------------------
Comparing benchmarks: BM_st_aha_find,BM_st_ahm_find
*       BM_st_aha_find                1000000  57.83 ms  57.83 ns  16.49 M
 +77.9% BM_st_ahm_find                1000000  102.9 ms  102.9 ns   9.27 M
------------------------------------------------------------------------------
BM_mt_ahm_miss                        1000000  2.937 ms  2.937 ns  324.7 M
BM_st_ahm_miss                        1000000  164.2 ms  164.2 ns  5.807 M
BM_mt_ahm_find_insert_mix             1000000  8.797 ms  8.797 ns  108.4 M
BM_mt_ahm_insert                      1000000  17.39 ms  17.39 ns  54.83 M
BM_st_ahm_insert                      1000000  106.8 ms  106.8 ns   8.93 M
BM_st_baseline_modulus_and_rando      1000000  6.223 ms  6.223 ns  153.2 M
*/
