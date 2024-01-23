/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <folly/SpinLock.h>

#include <thread>

#include <folly/Random.h>
#include <folly/Utility.h>
#include <folly/portability/Asm.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

namespace {

template <typename LOCK>
struct LockedVal {
  int ar[1024];
  LOCK lock;

  LockedVal() { memset(ar, 0, sizeof ar); }
};

template <typename LOCK>
void spinlockTestThread(size_t nthrs, LockedVal<LOCK>* v) {
  const size_t max = (1u << 16) //
      / folly::nextPowTwo(nthrs) //
      / (folly::kIsSanitizeThread ? 2 : 1);
  auto rng = folly::ThreadLocalPRNG();
  for (size_t i = 0; i < max; i++) {
    folly::asm_volatile_pause();
    std::unique_lock g(v->lock);

    EXPECT_THAT(v->ar, testing::Each(testing::Eq(v->ar[0])));

    int byte = folly::Random::rand32(rng);
    memset(v->ar, char(byte), sizeof v->ar);
  }
}

template <typename LOCK>
struct TryLockState {
  LOCK lock1;
  LOCK lock2;
  bool locked{false};
  uint64_t obtained{0};
  uint64_t failed{0};
};

template <typename LOCK>
void trylockTestThread(TryLockState<LOCK>* state, size_t count) {
  while (true) {
    folly::asm_volatile_pause();
    bool ret = state->lock2.try_lock();
    std::unique_lock g(state->lock1);
    if (state->obtained >= count) {
      if (ret) {
        state->lock2.unlock();
      }
      break;
    }

    if (ret) {
      // We got lock2.
      EXPECT_NE(state->locked, ret);
      ++state->obtained;
      state->locked = true;

      // Release lock1 and wait until at least one other thread fails to
      // obtain the lock2 before continuing.
      auto oldFailed = state->failed;
      while (state->failed == oldFailed && state->obtained < count) {
        state->lock1.unlock();
        folly::asm_volatile_pause();
        state->lock1.lock();
      }

      state->locked = false;
      state->lock2.unlock();
    } else {
      ++state->failed;
    }
  }
}

template <typename LOCK>
void correctnessTest() {
  size_t nthrs = folly::to_unsigned(sysconf(_SC_NPROCESSORS_ONLN) * 2);
  std::vector<std::thread> threads;
  LockedVal<LOCK> v;
  for (size_t i = 0; i < nthrs; ++i) {
    threads.push_back(std::thread(spinlockTestThread<LOCK>, nthrs, &v));
  }
  for (auto& t : threads) {
    t.join();
  }
}

template <typename LOCK>
void trylockTest() {
  int nthrs = sysconf(_SC_NPROCESSORS_ONLN) + 4;
  std::vector<std::thread> threads;
  TryLockState<LOCK> state;
  size_t count = 100;
  for (int i = 0; i < nthrs; ++i) {
    threads.push_back(std::thread(trylockTestThread<LOCK>, &state, count));
  }
  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(count, state.obtained);
  // Each time the code obtains lock2 it waits for another thread to fail
  // to acquire it.  The only time this might not happen is on the very last
  // loop when no other threads are left.
  EXPECT_GE(state.failed + 1, state.obtained);
}

} // namespace

TEST(SpinLock, Correctness) {
  correctnessTest<folly::SpinLock>();
}
TEST(SpinLock, TryLock) {
  trylockTest<folly::SpinLock>();
}
