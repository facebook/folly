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

#include "folly/SmallLocks.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <pthread.h>
#include <unistd.h>

#include <thread>

#include <gtest/gtest.h>

using std::string;
using folly::MicroSpinLock;
using folly::PicoSpinLock;
using folly::MSLGuard;

namespace {

struct LockedVal {
  int ar[1024];
  MicroSpinLock lock;

  LockedVal() {
    lock.init();
    memset(ar, 0, sizeof ar);
  }
};

// Compile time test for packed struct support (requires that both of
// these classes are POD).
struct ignore1 { MicroSpinLock msl; int16_t foo; } __attribute__((packed));
struct ignore2 { PicoSpinLock<uint32_t> psl; int16_t foo; }
  __attribute__((packed));
static_assert(sizeof(ignore1) == 3, "Size check failed");
static_assert(sizeof(ignore2) == 6, "Size check failed");

LockedVal v;
void splock_test() {

  const int max = 1000;
  unsigned int seed = (uintptr_t)pthread_self();
  for (int i = 0; i < max; i++) {
    asm("pause");
    MSLGuard g(v.lock);

    int first = v.ar[0];
    for (int i = 1; i < sizeof v.ar / sizeof i; ++i) {
      EXPECT_EQ(first, v.ar[i]);
    }

    int byte = rand_r(&seed);
    memset(v.ar, char(byte), sizeof v.ar);
  }
}

template<class T> struct PslTest {
  PicoSpinLock<T> lock;

  PslTest() { lock.init(); }

  void doTest() {
    T ourVal = rand() % (T(1) << (sizeof(T) * 8 - 1));
    for (int i = 0; i < 10000; ++i) {
      std::lock_guard<PicoSpinLock<T>> guard(lock);
      lock.setData(ourVal);
      for (int n = 0; n < 10; ++n) {
        asm volatile("pause");
        EXPECT_EQ(lock.getData(), ourVal);
      }
    }
  }
};

template<class T>
void doPslTest() {
  PslTest<T> testObj;

  const int nthrs = 17;
  std::vector<std::thread> threads;
  for (int i = 0; i < nthrs; ++i) {
    threads.push_back(std::thread(&PslTest<T>::doTest, &testObj));
  }
  for (auto& t : threads) {
    t.join();
  }
}

}

TEST(SmallLocks, SpinLockCorrectness) {
  EXPECT_EQ(sizeof(MicroSpinLock), 1);

  int nthrs = sysconf(_SC_NPROCESSORS_ONLN) * 2;
  std::vector<std::thread> threads;
  for (int i = 0; i < nthrs; ++i) {
    threads.push_back(std::thread(splock_test));
  }
  for (auto& t : threads) {
    t.join();
  }
}

TEST(SmallLocks, PicoSpinCorrectness) {
  doPslTest<int16_t>();
  doPslTest<uint16_t>();
  doPslTest<int32_t>();
  doPslTest<uint32_t>();
  doPslTest<int64_t>();
  doPslTest<uint64_t>();
}

TEST(SmallLocks, PicoSpinSigned) {
  typedef PicoSpinLock<int16_t,0> Lock;
  Lock val;
  val.init(-4);
  EXPECT_EQ(val.getData(), -4);

  {
    std::lock_guard<Lock> guard(val);
    EXPECT_EQ(val.getData(), -4);
    val.setData(-8);
    EXPECT_EQ(val.getData(), -8);
  }
  EXPECT_EQ(val.getData(), -8);
}
