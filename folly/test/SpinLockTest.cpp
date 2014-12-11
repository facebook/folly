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
#include <folly/SpinLock.h>

#include <gtest/gtest.h>
#include <thread>

using folly::SpinLock;
using folly::SpinLockGuard;

namespace {

struct LockedVal {
  int ar[1024];
  SpinLock lock;

  LockedVal() {
    memset(ar, 0, sizeof ar);
  }
};

LockedVal v;
void splock_test() {
  const int max = 1000;
  unsigned int seed = (uintptr_t)pthread_self();
  for (int i = 0; i < max; i++) {
    asm("pause");
    SpinLockGuard g(v.lock);

    int first = v.ar[0];
    for (size_t i = 1; i < sizeof v.ar / sizeof i; ++i) {
      EXPECT_EQ(first, v.ar[i]);
    }

    int byte = rand_r(&seed);
    memset(v.ar, char(byte), sizeof v.ar);
  }
}

struct TryLockState {
  SpinLock lock1;
  SpinLock lock2;
  bool locked{false};
  uint64_t obtained{0};
  uint64_t failed{0};
};
TryLockState tryState;

void trylock_test() {
  const int max = 1000;
  while (true) {
    asm("pause");
    SpinLockGuard g(tryState.lock1);
    if (tryState.obtained >= max) {
      break;
    }

    bool ret = tryState.lock2.trylock();
    EXPECT_NE(tryState.locked, ret);

    if (ret) {
      // We got lock2.
      ++tryState.obtained;
      tryState.locked = true;

      // Release lock1 and let other threads try to obtain lock2
      tryState.lock1.unlock();
      asm("pause");
      tryState.lock1.lock();

      tryState.locked = false;
      tryState.lock2.unlock();
    } else {
      ++tryState.failed;
    }
  }
}

} // unnamed namespace

TEST(SpinLock, Correctness) {
  int nthrs = sysconf(_SC_NPROCESSORS_ONLN) * 2;
  std::vector<std::thread> threads;
  for (int i = 0; i < nthrs; ++i) {
    threads.push_back(std::thread(splock_test));
  }
  for (auto& t : threads) {
    t.join();
  }
}

TEST(SpinLock, TryLock) {
  int nthrs = sysconf(_SC_NPROCESSORS_ONLN) * 2;
  std::vector<std::thread> threads;
  for (int i = 0; i < nthrs; ++i) {
    threads.push_back(std::thread(trylock_test));
  }
  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(1000, tryState.obtained);
  EXPECT_GT(tryState.failed, 0);
  LOG(INFO) << "failed count: " << tryState.failed;
}
