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

#include <folly/synchronization/Lock.h>

#include <folly/portability/GTest.h>

using namespace std::literals::chrono_literals;

class LockTest : public testing::Test {};

namespace {

using Clock = std::chrono::steady_clock;

class UnownedError : public std::runtime_error {
 public:
  UnownedError() : std::runtime_error::runtime_error("unowned") {}
};

class DeadlockError : public std::runtime_error {
 public:
  DeadlockError() : std::runtime_error::runtime_error("deadlock") {}
};

class MismatchError : public std::runtime_error {
 public:
  MismatchError() : std::runtime_error::runtime_error("mismatch") {}
};

struct Mutex {
  enum class Held { None, Unique, Shared, Upgrade };

  Held held = Held::None;
  Clock::time_point now = Clock::now();
  Clock::time_point locked_until = Clock::time_point::min();

  void lock() {
    op(Held::Unique);
  }
  bool try_lock() {
    return try_op(Held::Unique);
  }
  bool try_lock_for(Clock::duration timeout) {
    return try_op_for(Held::Unique, timeout);
  }
  bool try_lock_until(Clock::time_point deadline) {
    return try_op_until(Held::Unique, deadline);
  }
  void unlock() {
    unop(Held::Unique);
  }

  void lock_shared() {
    op(Held::Shared);
  }
  bool try_lock_shared() {
    return try_op(Held::Shared);
  }
  bool try_lock_shared_for(Clock::duration timeout) {
    return try_op_for(Held::Shared, timeout);
  }
  bool try_lock_shared_until(Clock::time_point deadline) {
    return try_op_until(Held::Shared, deadline);
  }
  void unlock_shared() {
    unop(Held::Shared);
  }

  void lock_upgrade() {
    op(Held::Upgrade);
  }
  bool try_lock_upgrade() {
    return try_op(Held::Upgrade);
  }
  bool try_lock_upgrade_for(Clock::duration timeout) {
    return try_op_for(Held::Upgrade, timeout);
  }
  bool try_lock_upgrade_until(Clock::time_point deadline) {
    return try_op_until(Held::Upgrade, deadline);
  }
  void unlock_upgrade() {
    unop(Held::Upgrade);
  }

  void unlock_and_lock_shared() {
    unlock();
    lock_shared();
  }
  void unlock_and_lock_upgrade() {
    unlock();
    lock_upgrade();
  }
  void unlock_upgrade_and_lock() {
    unlock_upgrade();
    lock();
  }
  void unlock_upgrade_and_lock_shared() {
    unlock_upgrade();
    lock_shared();
  }

  bool try_unlock_upgrade_and_lock() {
    return try_unlock_upgrade_and_lock_for(Clock::duration::zero());
  }
  bool try_unlock_upgrade_and_lock_for(Clock::duration timeout) {
    return try_unlock_upgrade_and_lock_until(now + timeout);
  }
  bool try_unlock_upgrade_and_lock_until(Clock::time_point deadline) {
    unlock_upgrade();
    return try_lock_until(deadline);
  }

  //  impl ...

  void op(Held h) {
    try_op(h) || (throw DeadlockError(), 0);
  }
  bool try_op(Held h) {
    return try_op_for(h, Clock::duration::zero());
  }
  bool try_op_for(Held h, Clock::duration timeout) {
    return try_op_until(h, now + timeout);
  }
  bool try_op_until(Held h, Clock::time_point deadline) {
    held == Held::None || (throw DeadlockError(), 0);
    auto const locked =
        locked_until != Clock::time_point::max() && locked_until <= deadline;
    held = locked ? h : held;
    now = locked ? locked_until : deadline;
    locked_until = locked ? Clock::time_point::max() : locked_until;
    return locked;
  }
  void unop(Held h) {
    held == Held::None && (throw UnownedError(), 0);
    held == h || (throw MismatchError(), 0);
    locked_until == Clock::time_point::min() && (throw UnownedError(), 0);
    held = Held::None;
    locked_until = Clock::time_point::min();
  }
};

bool is_locked_upgrade(Mutex& m) {
  return m.locked_until != Clock::time_point::min();
}

} // namespace

TEST_F(LockTest, unique_lock) {
  Mutex m;
  std::ignore = std::unique_lock(m);
  std::ignore = std::unique_lock(m, std::try_to_lock);
  std::ignore = std::unique_lock(m, std::defer_lock);
  m.lock();
  std::ignore = std::unique_lock(m, std::adopt_lock);
  std::ignore = std::unique_lock(m, 1s);
  std::ignore = std::unique_lock(m, m.now + 1s);
}

TEST_F(LockTest, shared_lock) {
  Mutex m;
  std::ignore = std::shared_lock(m);
  std::ignore = std::shared_lock(m, std::try_to_lock);
  std::ignore = std::shared_lock(m, std::defer_lock);
  m.lock_shared();
  std::ignore = std::shared_lock(m, std::adopt_lock);
  std::ignore = std::shared_lock(m, 1s);
  std::ignore = std::shared_lock(m, m.now + 1s);
}

TEST_F(LockTest, upgrade_lock_construct_default) {
  folly::upgrade_lock<Mutex> l;
  EXPECT_EQ(nullptr, l.mutex());
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_construct_mutex) {
  Mutex m;
  EXPECT_FALSE(is_locked_upgrade(m));
  folly::upgrade_lock<Mutex> l{m};
  EXPECT_TRUE(is_locked_upgrade(m));
  EXPECT_EQ(&m, l.mutex());
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_construct_mutex_defer) {
  Mutex m;
  EXPECT_FALSE(is_locked_upgrade(m));
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  EXPECT_FALSE(is_locked_upgrade(m));
  EXPECT_EQ(&m, l.mutex());
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_construct_mutex_try_pass) {
  Mutex m;
  EXPECT_FALSE(is_locked_upgrade(m));
  folly::upgrade_lock<Mutex> l{m, std::try_to_lock};
  EXPECT_TRUE(is_locked_upgrade(m));
  EXPECT_EQ(&m, l.mutex());
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_construct_mutex_try_fail) {
  Mutex m;
  m.locked_until = m.now + 1s;
  EXPECT_TRUE(is_locked_upgrade(m));
  folly::upgrade_lock<Mutex> l{m, std::try_to_lock};
  EXPECT_TRUE(is_locked_upgrade(m));
  EXPECT_EQ(&m, l.mutex());
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_construct_mutex_adopt) {
  Mutex m;
  m.held = Mutex::Held::Upgrade;
  m.locked_until = m.now + 1s;
  EXPECT_TRUE(is_locked_upgrade(m));
  folly::upgrade_lock<Mutex> l{m, std::adopt_lock};
  EXPECT_TRUE(is_locked_upgrade(m));
  EXPECT_EQ(&m, l.mutex());
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_construct_mutex_duration_pass) {
  Mutex m;
  m.locked_until = m.now + 1s;
  EXPECT_TRUE(is_locked_upgrade(m));
  folly::upgrade_lock<Mutex> l{m, 1000ms};
  EXPECT_TRUE(is_locked_upgrade(m));
  EXPECT_EQ(&m, l.mutex());
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_construct_mutex_duration_fail) {
  Mutex m;
  m.locked_until = m.now + 1s;
  EXPECT_TRUE(is_locked_upgrade(m));
  folly::upgrade_lock<Mutex> l{m, 999ms};
  EXPECT_TRUE(is_locked_upgrade(m));
  EXPECT_EQ(&m, l.mutex());
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_construct_mutex_time_point_pass) {
  Mutex m;
  m.locked_until = m.now + 1s;
  EXPECT_TRUE(is_locked_upgrade(m));
  folly::upgrade_lock<Mutex> l{m, m.now + 1000ms};
  EXPECT_TRUE(is_locked_upgrade(m));
  EXPECT_EQ(&m, l.mutex());
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_construct_mutex_time_point_fail) {
  Mutex m;
  m.locked_until = m.now + 1s;
  EXPECT_TRUE(is_locked_upgrade(m));
  folly::upgrade_lock<Mutex> l{m, m.now + 999ms};
  EXPECT_TRUE(is_locked_upgrade(m));
  EXPECT_EQ(&m, l.mutex());
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_move_construct) {
  Mutex m;
  folly::upgrade_lock<Mutex> l0{m};
  EXPECT_EQ(&m, l0.mutex());
  EXPECT_TRUE(l0.owns_lock());
  folly::upgrade_lock<Mutex> l1{std::move(l0)};
  EXPECT_EQ(nullptr, l0.mutex());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(&m, l1.mutex());
  EXPECT_TRUE(l1.owns_lock());
  l1.unlock();
  EXPECT_EQ(&m, l1.mutex());
  EXPECT_FALSE(l1.owns_lock());
  folly::upgrade_lock<Mutex> l2{std::move(l1)};
  EXPECT_EQ(nullptr, l1.mutex());
  EXPECT_FALSE(l1.owns_lock());
  EXPECT_EQ(&m, l2.mutex());
  EXPECT_FALSE(l2.owns_lock());
}

TEST_F(LockTest, upgrade_lock_destruct) {
  Mutex m;
  std::unique_ptr<folly::upgrade_lock<Mutex>> lock;
  EXPECT_FALSE(is_locked_upgrade(m));
  lock = std::make_unique<folly::upgrade_lock<Mutex>>(m);
  EXPECT_TRUE(is_locked_upgrade(m));
  lock = nullptr;
  EXPECT_FALSE(is_locked_upgrade(m));
  lock = std::make_unique<folly::upgrade_lock<Mutex>>(m, std::defer_lock);
  EXPECT_FALSE(is_locked_upgrade(m));
  lock = nullptr;
  EXPECT_FALSE(is_locked_upgrade(m));
}

TEST_F(LockTest, upgrade_lock_move_assign) {
  Mutex m0;
  Mutex m1;
  folly::upgrade_lock<Mutex> l0{m0};
  EXPECT_EQ(&m0, l0.mutex());
  EXPECT_TRUE(l0.owns_lock());
  folly::upgrade_lock<Mutex> l1{m1};
  EXPECT_EQ(&m1, l1.mutex());
  EXPECT_TRUE(l1.owns_lock());
  EXPECT_TRUE(is_locked_upgrade(m0));
  EXPECT_TRUE(is_locked_upgrade(m1));
  l1 = std::move(l0);
  EXPECT_TRUE(is_locked_upgrade(m0));
  EXPECT_FALSE(is_locked_upgrade(m1));
  EXPECT_EQ(nullptr, l0.mutex());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(&m0, l1.mutex());
  EXPECT_TRUE(l1.owns_lock());
  l1.unlock();
  EXPECT_FALSE(is_locked_upgrade(m0));
  EXPECT_FALSE(is_locked_upgrade(m1));
}

TEST_F(LockTest, upgrade_lock_pass) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  l.lock();
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_fail) {
  Mutex m;
  m.locked_until = Clock::time_point::max();
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  EXPECT_THROW(l.lock(), DeadlockError);
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_owns) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m};
  EXPECT_THROW(l.lock(), std::system_error);
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_empty) {
  folly::upgrade_lock<Mutex> l{};
  EXPECT_THROW(l.lock(), std::system_error);
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_pass) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  EXPECT_TRUE(l.try_lock());
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_fail) {
  Mutex m;
  m.locked_until = m.now + 1s;
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  EXPECT_FALSE(l.try_lock());
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_owns) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m};
  EXPECT_THROW(l.try_lock(), std::system_error);
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_empty) {
  folly::upgrade_lock<Mutex> l{};
  EXPECT_THROW(l.try_lock(), std::system_error);
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_for_pass) {
  Mutex m;
  m.locked_until = m.now + 1s;
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  EXPECT_TRUE(l.try_lock_for(2s));
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_for_fail) {
  Mutex m;
  m.locked_until = m.now + 1s;
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  EXPECT_FALSE(l.try_lock_for(500ms));
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_for_owns) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m};
  EXPECT_THROW(l.try_lock_for(0s), std::system_error);
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_for_empty) {
  folly::upgrade_lock<Mutex> l{};
  EXPECT_THROW(l.try_lock_for(0s), std::system_error);
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_until_pass) {
  Mutex m;
  m.locked_until = m.now + 1s;
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  EXPECT_TRUE(l.try_lock_until(m.now + 2s));
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_until_fail) {
  Mutex m;
  m.locked_until = m.now + 1s;
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  EXPECT_FALSE(l.try_lock_until(m.now + 500ms));
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_until_owns) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m};
  EXPECT_THROW(l.try_lock_until(m.now), std::system_error);
  EXPECT_TRUE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_try_lock_until_for_empty) {
  folly::upgrade_lock<Mutex> l{};
  EXPECT_THROW(l.try_lock_until(Clock::now()), std::system_error);
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_unlock_owns) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m};
  l.unlock();
  EXPECT_FALSE(l.owns_lock());
}

TEST_F(LockTest, upgrade_lock_unlock_unlocked) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m, std::adopt_lock};
  EXPECT_THROW(l.unlock(), UnownedError);
  l.release();
}

TEST_F(LockTest, upgrade_lock_unlock_unowned) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  EXPECT_THROW(l.unlock(), std::system_error);
}

TEST_F(LockTest, upgrade_lock_unlock_empty) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{};
  EXPECT_THROW(l.unlock(), std::system_error);
}

TEST_F(LockTest, upgrade_lock_release_empty) {
  folly::upgrade_lock<Mutex> l{};
  auto r = l.release();
  EXPECT_EQ(nullptr, r);
  EXPECT_EQ(nullptr, l.mutex());
  EXPECT_FALSE(l.owns_lock());
  EXPECT_EQ(nullptr, l.release());
}

TEST_F(LockTest, upgrade_lock_release_unowned) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m, std::defer_lock};
  auto r = l.release();
  EXPECT_FALSE(is_locked_upgrade(m));
  EXPECT_EQ(&m, r);
  EXPECT_EQ(nullptr, l.mutex());
  EXPECT_FALSE(l.owns_lock());
  EXPECT_EQ(nullptr, l.release());
}

TEST_F(LockTest, upgrade_lock_release_owns) {
  Mutex m;
  folly::upgrade_lock<Mutex> l{m};
  auto r = l.release();
  EXPECT_TRUE(is_locked_upgrade(m));
  EXPECT_EQ(&m, r);
  EXPECT_EQ(nullptr, l.mutex());
  EXPECT_FALSE(l.owns_lock());
  EXPECT_EQ(nullptr, l.release());
}

TEST_F(LockTest, upgrade_lock_swap) {
  Mutex m0;
  Mutex m1;
  folly::upgrade_lock<Mutex> l0{m0};
  folly::upgrade_lock<Mutex> l1{m1, std::defer_lock};
  swap(l0, l1);
  EXPECT_EQ(&m1, l0.mutex());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(&m0, l1.mutex());
  EXPECT_TRUE(l1.owns_lock());
}

TEST_F(LockTest, unique_lock_transition_to_shared_lock) {
  Mutex m;
  std::unique_lock<Mutex> l0{m};
  EXPECT_TRUE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Unique, m.held);
  std::shared_lock<Mutex> l1 = folly::transition_to_shared_lock(l0);
  EXPECT_TRUE(l1.owns_lock());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Shared, m.held);
}

TEST_F(LockTest, unique_lock_transition_to_upgrade_lock) {
  Mutex m;
  std::unique_lock<Mutex> l0{m};
  EXPECT_TRUE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Unique, m.held);
  folly::upgrade_lock<Mutex> l1 = folly::transition_to_upgrade_lock(l0);
  EXPECT_TRUE(l1.owns_lock());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Upgrade, m.held);
}

TEST_F(LockTest, upgrade_lock_transition_to_unique_lock) {
  Mutex m;
  folly::upgrade_lock<Mutex> l0{m};
  EXPECT_TRUE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Upgrade, m.held);
  std::unique_lock<Mutex> l1 = folly::transition_to_unique_lock(l0);
  EXPECT_TRUE(l1.owns_lock());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Unique, m.held);
}

TEST_F(LockTest, upgrade_lock_transition_to_shared_lock) {
  Mutex m;
  folly::upgrade_lock<Mutex> l0{m};
  EXPECT_TRUE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Upgrade, m.held);
  std::shared_lock<Mutex> l1 = folly::transition_to_shared_lock(l0);
  EXPECT_TRUE(l1.owns_lock());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Shared, m.held);
}

TEST_F(LockTest, upgrade_lock_try_transition_to_unique_lock) {
  Mutex m;
  folly::upgrade_lock<Mutex> l0{m};
  EXPECT_TRUE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Upgrade, m.held);
  std::unique_lock<Mutex> l1 = folly::try_transition_to_unique_lock(l0);
  EXPECT_TRUE(l1.owns_lock());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Unique, m.held);
}

TEST_F(LockTest, upgrade_lock_try_transition_to_unique_lock_for) {
  Mutex m;
  folly::upgrade_lock<Mutex> l0{m};
  EXPECT_TRUE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Upgrade, m.held);
  std::unique_lock<Mutex> l1 = folly::try_transition_to_unique_lock_for(l0, 1s);
  EXPECT_TRUE(l1.owns_lock());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Unique, m.held);
}

TEST_F(LockTest, upgrade_lock_try_transition_to_unique_lock_until) {
  Mutex m;
  folly::upgrade_lock<Mutex> l0{m};
  EXPECT_TRUE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Upgrade, m.held);
  std::unique_lock<Mutex> l1 =
      folly::try_transition_to_unique_lock_until(l0, m.now + 1s);
  EXPECT_TRUE(l1.owns_lock());
  EXPECT_FALSE(l0.owns_lock());
  EXPECT_EQ(Mutex::Held::Unique, m.held);
}
