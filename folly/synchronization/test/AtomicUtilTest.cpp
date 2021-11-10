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

#include <folly/synchronization/AtomicUtil.h>

#include <utility>

#include <folly/Benchmark.h>
#include <folly/Portability.h>
#include <folly/Utility.h>
#include <folly/functional/Invoke.h>
#include <folly/portability/GTest.h>

namespace folly {

class AtomicCompareExchangeSuccTest : public testing::Test {};

TEST_F(AtomicCompareExchangeSuccTest, examples) {
  using detail::atomic_compare_exchange_succ;

  auto const relaxed = std::memory_order_relaxed;
  auto const consume = std::memory_order_consume;
  auto const acquire = std::memory_order_acquire;
  auto const release = std::memory_order_release;
  auto const acq_rel = std::memory_order_acq_rel;
  auto const seq_cst = std::memory_order_seq_cst;

  // noop table
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(false, relaxed, relaxed));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(false, consume, relaxed));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(false, acquire, relaxed));
  EXPECT_EQ(release, atomic_compare_exchange_succ(false, release, relaxed));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(false, acq_rel, relaxed));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(false, seq_cst, relaxed));
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(false, relaxed, consume));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(false, consume, consume));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(false, acquire, consume));
  EXPECT_EQ(release, atomic_compare_exchange_succ(false, release, consume));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(false, acq_rel, consume));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(false, seq_cst, consume));
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(false, relaxed, acquire));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(false, consume, acquire));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(false, acquire, acquire));
  EXPECT_EQ(release, atomic_compare_exchange_succ(false, release, acquire));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(false, acq_rel, acquire));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(false, seq_cst, acquire));
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(false, relaxed, seq_cst));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(false, consume, seq_cst));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(false, acquire, seq_cst));
  EXPECT_EQ(release, atomic_compare_exchange_succ(false, release, seq_cst));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(false, acq_rel, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(false, seq_cst, seq_cst));

  // xform table
  EXPECT_EQ(relaxed, atomic_compare_exchange_succ(true, relaxed, relaxed));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(true, consume, relaxed));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, acquire, relaxed));
  EXPECT_EQ(release, atomic_compare_exchange_succ(true, release, relaxed));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, acq_rel, relaxed));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, seq_cst, relaxed));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(true, relaxed, consume));
  EXPECT_EQ(consume, atomic_compare_exchange_succ(true, consume, consume));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, acquire, consume));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, release, consume));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, acq_rel, consume));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, seq_cst, consume));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, relaxed, acquire));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, consume, acquire));
  EXPECT_EQ(acquire, atomic_compare_exchange_succ(true, acquire, acquire));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, release, acquire));
  EXPECT_EQ(acq_rel, atomic_compare_exchange_succ(true, acq_rel, acquire));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, seq_cst, acquire));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, relaxed, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, consume, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, acquire, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, release, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, acq_rel, seq_cst));
  EXPECT_EQ(seq_cst, atomic_compare_exchange_succ(true, seq_cst, seq_cst));

  // properties
  for (auto succ : {relaxed, consume, acquire, release, acq_rel, seq_cst}) {
    SCOPED_TRACE(static_cast<int>(succ));
    for (auto fail : {relaxed, consume, acquire, seq_cst}) {
      SCOPED_TRACE(static_cast<int>(fail));
      EXPECT_EQ(succ, atomic_compare_exchange_succ(false, succ, fail));
      auto const sfix = atomic_compare_exchange_succ(true, succ, fail);
      EXPECT_GE(sfix, succ);
      EXPECT_GE(sfix, fail);
      EXPECT_TRUE(fail != relaxed || sfix == succ);
      EXPECT_TRUE(fail == relaxed || succ != release || sfix != release);
    }
  }
}

namespace access {
FOLLY_CREATE_FREE_INVOKER_SUITE(atomic_fetch_set, folly);
FOLLY_CREATE_FREE_INVOKER_SUITE(atomic_fetch_reset, folly);
FOLLY_CREATE_FREE_INVOKER_SUITE(atomic_fetch_set_fallback, folly::detail);
FOLLY_CREATE_FREE_INVOKER_SUITE(atomic_fetch_reset_fallback, folly::detail);
} // namespace access

namespace {

template <typename I>
struct with_seq_cst : private I {
  explicit with_seq_cst(I i) noexcept : I{i} {}
  template <typename... A>
  constexpr decltype(auto) operator()(A&&... a) const noexcept {
    return I::operator()(static_cast<A&&>(a)..., std::memory_order_seq_cst);
  }
};

template <typename Integer, typename Op = access::atomic_fetch_set_fn>
void atomic_fetch_set_basic(Op fetch_set = {}) {
  {
    auto&& atomic = std::atomic<Integer>{0};
    EXPECT_EQ(fetch_set(atomic, 0), false);
    EXPECT_EQ(fetch_set(atomic, 1), false);
    EXPECT_EQ(atomic.load(), 0b11);
    EXPECT_EQ(fetch_set(atomic, 2), false);
    EXPECT_EQ(atomic.load(), 0b111);
  }

  {
    auto&& atomic = std::atomic<Integer>{0b1};
    EXPECT_EQ(fetch_set(atomic, 0), true);
    EXPECT_EQ(fetch_set(atomic, 0), true);
    EXPECT_EQ(fetch_set(atomic, 1), false);
    EXPECT_EQ(atomic.load(), 0b11);
    EXPECT_EQ(fetch_set(atomic, 2), false);
    EXPECT_EQ(atomic.load(), 0b111);
  }

  {
    for (auto i = 0; i < 100000; ++i) {
      // call makeUnpredictable() to ensure that the bit integer does not get
      // optimized away.  This is testing the feasability of this code in
      // situations where bit is not known at compile time and will likely force
      // a register load
      auto&& atomic = std::atomic<Integer>{0};
      auto&& bit = 0;
      folly::makeUnpredictable(bit);

      EXPECT_EQ(fetch_set(atomic, bit), false);
      EXPECT_EQ(fetch_set(atomic, bit + 1), false);
      EXPECT_EQ(atomic.load(), 0b11);
      EXPECT_EQ(fetch_set(atomic, bit + 2), false);
      EXPECT_EQ(atomic.load(), 0b111);
    }
  }
}

template <typename Integer, typename Op = access::atomic_fetch_reset_fn>
void atomic_fetch_reset_basic(Op fetch_reset = {}) {
  {
    auto&& atomic = std::atomic<Integer>{0};
    EXPECT_EQ(fetch_reset(atomic, 0), false);
    EXPECT_EQ(fetch_reset(atomic, 1), false);
    atomic.store(0b11);
    EXPECT_EQ(fetch_reset(atomic, 0), true);
    EXPECT_EQ(fetch_reset(atomic, 1), true);
    EXPECT_EQ(atomic.load(), 0);
  }

  {
    auto&& atomic = std::atomic<Integer>{0};
    EXPECT_EQ(fetch_reset(atomic, 0), false);
    EXPECT_EQ(fetch_reset(atomic, 1), false);
    atomic.store(0b11);
    EXPECT_EQ(fetch_reset(atomic, 1), true);
    EXPECT_EQ(fetch_reset(atomic, 0), true);
    EXPECT_EQ(atomic.load(), 0);
  }
}

template <typename Integer>
class Atomic {
 public:
  Integer fetch_or(
      Integer value, std::memory_order = std::memory_order_seq_cst) {
    ++counts.set;
    return std::exchange(integer_, integer_ | value);
  }
  Integer fetch_and(
      Integer value, std::memory_order = std::memory_order_seq_cst) {
    ++counts.reset;
    return std::exchange(integer_, integer_ & value);
  }

  Integer load(std::memory_order = std::memory_order_seq_cst) {
    return integer_;
  }

  Integer integer_{0};

  struct counts_ {
    size_t set{0};
    size_t reset{0};
  };
  counts_ counts;
};

template <typename Integer, typename Op = access::atomic_fetch_set_fn>
void atomic_fetch_set_non_std_atomic(Op fetch_set = {}) {
  auto atomic = Atomic<Integer>{};
  auto& sets = atomic.counts.set;
  auto& resets = atomic.counts.reset;

  fetch_set(atomic, 0);
  EXPECT_EQ(sets, 1);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(atomic.integer_, 0b1);

  fetch_set(atomic, 2);
  EXPECT_EQ(sets, 2);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(atomic.integer_, 0b101);
}

template <typename Integer, typename Op = access::atomic_fetch_reset_fn>
void atomic_fetch_reset_non_std_atomic(Op fetch_reset = {}) {
  auto atomic = Atomic<Integer>{};
  auto& sets = atomic.counts.set;
  auto& resets = atomic.counts.reset;
  atomic.integer_ = 0b111;

  fetch_reset(atomic, 0);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 1);
  EXPECT_EQ(atomic.integer_, 0b110);

  fetch_reset(atomic, 2);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 2);
  EXPECT_EQ(atomic.integer_, 0b010);
}
} // namespace

class AtomicFetchSetTest : public ::testing::Test {};
class AtomicFetchResetTest : public ::testing::Test {};

TEST_F(AtomicFetchSetTest, Basic) {
  atomic_fetch_set_basic<std::uint16_t>();
  atomic_fetch_set_basic<std::uint32_t>();
  atomic_fetch_set_basic<std::uint64_t>();
  atomic_fetch_set_basic<std::uint8_t>();
}

TEST_F(AtomicFetchResetTest, Basic) {
  atomic_fetch_reset_basic<std::uint16_t>();
  atomic_fetch_reset_basic<std::uint32_t>();
  atomic_fetch_reset_basic<std::uint64_t>();
  atomic_fetch_reset_basic<std::uint8_t>();
}

TEST_F(AtomicFetchSetTest, EnsureFetchOrUsed) {
  atomic_fetch_set_non_std_atomic<std::uint8_t>();
  atomic_fetch_set_non_std_atomic<std::uint16_t>();
  atomic_fetch_set_non_std_atomic<std::uint32_t>();
  atomic_fetch_set_non_std_atomic<std::uint64_t>();
}

TEST_F(AtomicFetchResetTest, EnsureFetchAndUsed) {
  atomic_fetch_reset_non_std_atomic<std::uint8_t>();
  atomic_fetch_reset_non_std_atomic<std::uint16_t>();
  atomic_fetch_reset_non_std_atomic<std::uint32_t>();
  atomic_fetch_reset_non_std_atomic<std::uint64_t>();
}

TEST_F(AtomicFetchSetTest, FetchSetFallback) {
  auto fetch_set = with_seq_cst{access::atomic_fetch_set_fallback};

  atomic_fetch_set_basic<std::uint16_t>(fetch_set);
  atomic_fetch_set_basic<std::uint32_t>(fetch_set);
  atomic_fetch_set_basic<std::uint64_t>(fetch_set);
  atomic_fetch_set_basic<std::uint8_t>(fetch_set);

  atomic_fetch_set_non_std_atomic<std::uint8_t>(fetch_set);
  atomic_fetch_set_non_std_atomic<std::uint16_t>(fetch_set);
  atomic_fetch_set_non_std_atomic<std::uint32_t>(fetch_set);
  atomic_fetch_set_non_std_atomic<std::uint64_t>(fetch_set);
}

TEST_F(AtomicFetchResetTest, FetchResetFallback) {
  auto fetch_reset = with_seq_cst{access::atomic_fetch_reset_fallback};

  atomic_fetch_reset_basic<std::uint16_t>(fetch_reset);
  atomic_fetch_reset_basic<std::uint32_t>(fetch_reset);
  atomic_fetch_reset_basic<std::uint64_t>(fetch_reset);
  atomic_fetch_reset_basic<std::uint8_t>(fetch_reset);

  atomic_fetch_reset_non_std_atomic<std::uint8_t>(fetch_reset);
  atomic_fetch_reset_non_std_atomic<std::uint16_t>(fetch_reset);
  atomic_fetch_reset_non_std_atomic<std::uint32_t>(fetch_reset);
  atomic_fetch_reset_non_std_atomic<std::uint64_t>(fetch_reset);
}

TEST_F(AtomicFetchSetTest, FetchSetDefault) {
  auto fetch_set = access::atomic_fetch_set;

  atomic_fetch_set_basic<std::uint16_t>(fetch_set);
  atomic_fetch_set_basic<std::uint32_t>(fetch_set);
  atomic_fetch_set_basic<std::uint64_t>(fetch_set);
  atomic_fetch_set_basic<std::uint8_t>(fetch_set);

  atomic_fetch_set_non_std_atomic<std::uint8_t>(fetch_set);
  atomic_fetch_set_non_std_atomic<std::uint16_t>(fetch_set);
  atomic_fetch_set_non_std_atomic<std::uint32_t>(fetch_set);
  atomic_fetch_set_non_std_atomic<std::uint64_t>(fetch_set);
}

TEST_F(AtomicFetchResetTest, FetchResetDefault) {
  auto fetch_reset = access::atomic_fetch_reset;

  atomic_fetch_reset_basic<std::uint16_t>(fetch_reset);
  atomic_fetch_reset_basic<std::uint32_t>(fetch_reset);
  atomic_fetch_reset_basic<std::uint64_t>(fetch_reset);
  atomic_fetch_reset_basic<std::uint8_t>(fetch_reset);

  atomic_fetch_reset_non_std_atomic<std::uint8_t>(fetch_reset);
  atomic_fetch_reset_non_std_atomic<std::uint16_t>(fetch_reset);
  atomic_fetch_reset_non_std_atomic<std::uint32_t>(fetch_reset);
  atomic_fetch_reset_non_std_atomic<std::uint64_t>(fetch_reset);
}

} // namespace folly
