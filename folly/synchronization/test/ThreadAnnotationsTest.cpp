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

#include <folly/synchronization/ThreadAnnotations.h>

#include <atomic>
#include <thread>
#include <vector>

#include <folly/portability/GTest.h>

namespace {

// Counter exercising AnnotatedMutex + AnnotatedLockGuard + GUARDED_BY.
// On Clang the FOLLY_TS_GUARDED_BY annotation is checked at compile time
// under -Wthread-safety; if `value_` were touched without holding `mu_`,
// or if `add()` were called while holding `mu_` (since it requires the
// negative capability !mu_), the build would fail.
class GuardedCounter {
 public:
  void add(int n) FOLLY_TS_REQUIRES(!mu_) {
    folly::AnnotatedLockGuard lock(mu_);
    value_ += n;
  }

  int get() const FOLLY_TS_REQUIRES(!mu_) {
    folly::AnnotatedLockGuard lock(mu_);
    return value_;
  }

 private:
  mutable folly::AnnotatedMutex mu_;
  int value_ FOLLY_TS_GUARDED_BY(mu_) = 0;
};

} // namespace

TEST(AnnotatedMutexTest, LockUnlock) {
  folly::AnnotatedMutex mu;
  mu.lock();
  // Verify the mutex is held by attempting try_lock from another thread;
  // calling try_lock on an already-owned mutex from the same thread is UB.
  bool couldAcquire = false;
  std::thread([&] {
    if (mu.try_lock()) {
      couldAcquire = true;
      mu.unlock();
    }
  }).join();
  EXPECT_FALSE(couldAcquire);
  mu.unlock();
  if (mu.try_lock()) { // available after unlock
    mu.unlock();
  } else {
    FAIL() << "mutex should be available after unlock";
  }
}

TEST(AnnotatedMutexTest, TryLock) {
  folly::AnnotatedMutex mu;
  // `if` makes the acquired branch visible to the analyzer; `EXPECT_TRUE`
  // alone does not propagate the "acquired" state.
  if (mu.try_lock()) {
    mu.unlock();
  } else {
    FAIL() << "try_lock should succeed on a fresh mutex";
  }
}

TEST(AnnotatedLockGuardTest, ScopedAcquireRelease) {
  folly::AnnotatedMutex mu;
  {
    folly::AnnotatedLockGuard lock(mu);
    // Lock is held within this scope.
  }
  // Lock released; should be acquirable again.
  if (mu.try_lock()) {
    mu.unlock();
  } else {
    FAIL() << "lock should be released after AnnotatedLockGuard scope ends";
  }
}

TEST(AnnotatedLockGuardTest, GuardedDataSerializesWrites) {
  // 8 threads each add 1000 times. Without serialization the final value
  // would race; with proper serialization it must be exactly 8000.
  GuardedCounter counter;
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&counter]() {
      for (int i = 0; i < 1000; ++i) {
        counter.add(1);
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  EXPECT_EQ(8000, counter.get());
}

TEST(AnnotatedLockGuardTest, MultipleMutexInstancesAreIndependent) {
  folly::AnnotatedMutex mu1;
  folly::AnnotatedMutex mu2;

  folly::AnnotatedLockGuard lock1(mu1);
  // mu2 should still be free even though mu1 is held. The analyzer also
  // verifies via `if` that we only unlock mu2 if try_lock succeeded.
  if (mu2.try_lock()) {
    mu2.unlock();
  } else {
    FAIL() << "different mutex instances should not block each other";
  }
}

TEST(AnnotatedLockGuardTest, TemplateDeducesFromStdMutex) {
  // The fallback path on non-Clang compilers makes AnnotatedLockGuard a
  // template alias for std::lock_guard, so it must work with std::mutex.
  // On Clang the templated guard also accepts std::mutex (the analyzer
  // simply doesn't track it as a capability).
  std::mutex mu;
  {
    folly::AnnotatedLockGuard<std::mutex> lock(mu);
    bool acquired = false;
    std::thread([&] {
      acquired = mu.try_lock();
      if (acquired) {
        mu.unlock();
      }
    }).join();
    EXPECT_FALSE(acquired);
  }
  // Guard destroyed — mutex must be released.
  EXPECT_TRUE(mu.try_lock());
  mu.unlock();
}
