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

#include <folly/synchronization/AtomicUtil.h>

#include <utility>

#include <folly/Benchmark.h>
#include <folly/Portability.h>
#include <folly/Utility.h>
#include <folly/functional/Invoke.h>
#include <folly/portability/GTest.h>

static constexpr auto relaxed = std::memory_order_relaxed;
static constexpr auto consume = std::memory_order_consume;
static constexpr auto acquire = std::memory_order_acquire;
static constexpr auto release = std::memory_order_release;
static constexpr auto acq_rel = std::memory_order_acq_rel;
static constexpr auto seq_cst = std::memory_order_seq_cst;

namespace folly {

class AtomicCompareExchangeSuccTest : public testing::Test {};

TEST_F(AtomicCompareExchangeSuccTest, examples) {
  using detail::atomic_compare_exchange_succ;

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

namespace {

template <typename L>
struct with_order : private L {
  std::memory_order o_;
  explicit with_order(std::memory_order o, L i) noexcept : L{i}, o_{o} {}
  template <typename... A>
  constexpr decltype(auto) operator()(A&&... a) const noexcept {
    return L::operator()(static_cast<A&&>(a)..., o_);
  }
};

template <typename Integer, typename Op>
void atomic_fetch_set_basic(Op fetch_set) {
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

template <typename Integer, typename Op>
void atomic_fetch_reset_basic(Op fetch_reset) {
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

template <typename Integer, typename Op>
void atomic_fetch_flip_basic(Op fetch_flip) {
  {
    auto&& atomic = std::atomic<Integer>{0};
    EXPECT_EQ(fetch_flip(atomic, 0), false);
    EXPECT_EQ(fetch_flip(atomic, 1), false);
    atomic.store(0b11);
    EXPECT_EQ(fetch_flip(atomic, 0), true);
    EXPECT_EQ(fetch_flip(atomic, 1), true);
    EXPECT_EQ(atomic.load(), 0);
  }

  {
    auto&& atomic = std::atomic<Integer>{0};
    EXPECT_EQ(fetch_flip(atomic, 0), false);
    EXPECT_EQ(fetch_flip(atomic, 1), false);
    atomic.store(0b10);
    EXPECT_EQ(fetch_flip(atomic, 1), true);
    EXPECT_EQ(fetch_flip(atomic, 0), false);
    EXPECT_EQ(atomic.load(), 0b01);
  }
}

template <typename Integer>
class Atomic {
 public:
  using value_type = Integer;

  Integer fetch_or(Integer value, std::memory_order = seq_cst) {
    ++counts.set;
    return std::exchange(integer_, integer_ | value);
  }
  Integer fetch_and(Integer value, std::memory_order = seq_cst) {
    ++counts.reset;
    return std::exchange(integer_, integer_ & value);
  }
  Integer fetch_xor(Integer value, std::memory_order = seq_cst) {
    ++counts.flip;
    return std::exchange(integer_, integer_ ^ value);
  }

  Integer load(std::memory_order = seq_cst) { return integer_; }

  Integer integer_{0};

  struct counts_ {
    size_t set{0};
    size_t reset{0};
    size_t flip{0};
  };
  counts_ counts;
};

template <typename Integer, typename Op>
void atomic_fetch_set_non_std_atomic(Op fetch_set) {
  auto atomic = Atomic<Integer>{};
  auto& sets = atomic.counts.set;
  auto& resets = atomic.counts.reset;
  auto& flips = atomic.counts.flip;

  fetch_set(atomic, 0);
  EXPECT_EQ(sets, 1);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(flips, 0);
  EXPECT_EQ(atomic.integer_, 0b1);

  fetch_set(atomic, 2);
  EXPECT_EQ(sets, 2);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(flips, 0);
  EXPECT_EQ(atomic.integer_, 0b101);
}

template <typename Integer, typename Op>
void atomic_fetch_reset_non_std_atomic(Op fetch_reset) {
  auto atomic = Atomic<Integer>{};
  auto& sets = atomic.counts.set;
  auto& resets = atomic.counts.reset;
  auto& flips = atomic.counts.flip;
  atomic.integer_ = 0b111;

  fetch_reset(atomic, 0);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 1);
  EXPECT_EQ(flips, 0);
  EXPECT_EQ(atomic.integer_, 0b110);

  fetch_reset(atomic, 2);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 2);
  EXPECT_EQ(flips, 0);
  EXPECT_EQ(atomic.integer_, 0b010);
}

template <typename Integer, typename Op>
void atomic_fetch_flip_non_std_atomic(Op fetch_flip) {
  auto atomic = Atomic<Integer>{};
  auto& sets = atomic.counts.set;
  auto& resets = atomic.counts.reset;
  auto& flips = atomic.counts.flip;
  atomic.integer_ = 0b110;

  fetch_flip(atomic, 0);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(flips, 1);
  EXPECT_EQ(atomic.integer_, 0b111);

  fetch_flip(atomic, 2);
  EXPECT_EQ(sets, 0);
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(flips, 2);
  EXPECT_EQ(atomic.integer_, 0b011);
}
} // namespace

class AtomicFetchSetTest : public ::testing::Test {};
class AtomicFetchResetTest : public ::testing::Test {};
class AtomicFetchFlipTest : public ::testing::Test {};

TEST_F(AtomicFetchSetTest, Basic) {
  auto op = with_order{seq_cst, folly::atomic_fetch_set};

  atomic_fetch_set_basic<std::uint16_t>(op);
  atomic_fetch_set_basic<std::uint32_t>(op);
  atomic_fetch_set_basic<std::uint64_t>(op);
  atomic_fetch_set_basic<std::uint8_t>(op);
}

TEST_F(AtomicFetchResetTest, Basic) {
  auto op = with_order{seq_cst, folly::atomic_fetch_reset};

  atomic_fetch_reset_basic<std::uint16_t>(op);
  atomic_fetch_reset_basic<std::uint32_t>(op);
  atomic_fetch_reset_basic<std::uint64_t>(op);
  atomic_fetch_reset_basic<std::uint8_t>(op);
}

TEST_F(AtomicFetchFlipTest, Basic) {
  auto op = with_order{seq_cst, folly::atomic_fetch_flip};

  atomic_fetch_flip_basic<std::uint16_t>(op);
  atomic_fetch_flip_basic<std::uint32_t>(op);
  atomic_fetch_flip_basic<std::uint64_t>(op);
  atomic_fetch_flip_basic<std::uint8_t>(op);
}

TEST_F(AtomicFetchSetTest, BasicRelaxed) {
  auto op = with_order{relaxed, folly::atomic_fetch_set};

  atomic_fetch_set_basic<std::uint16_t>(op);
  atomic_fetch_set_basic<std::uint32_t>(op);
  atomic_fetch_set_basic<std::uint64_t>(op);
  atomic_fetch_set_basic<std::uint8_t>(op);
}

TEST_F(AtomicFetchResetTest, BasicRelaxed) {
  auto op = with_order{relaxed, folly::atomic_fetch_reset};

  atomic_fetch_reset_basic<std::uint16_t>(op);
  atomic_fetch_reset_basic<std::uint32_t>(op);
  atomic_fetch_reset_basic<std::uint64_t>(op);
  atomic_fetch_reset_basic<std::uint8_t>(op);
}

TEST_F(AtomicFetchFlipTest, BasicRelaxed) {
  auto op = with_order{relaxed, folly::atomic_fetch_flip};

  atomic_fetch_flip_basic<std::uint16_t>(op);
  atomic_fetch_flip_basic<std::uint32_t>(op);
  atomic_fetch_flip_basic<std::uint64_t>(op);
  atomic_fetch_flip_basic<std::uint8_t>(op);
}

TEST_F(AtomicFetchSetTest, EnsureFetchOrUsed) {
  auto op = with_order{seq_cst, folly::atomic_fetch_set};

  atomic_fetch_set_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint64_t>(op);
}

TEST_F(AtomicFetchResetTest, EnsureFetchAndUsed) {
  auto op = with_order{seq_cst, folly::atomic_fetch_reset};

  atomic_fetch_reset_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint64_t>(op);
}

TEST_F(AtomicFetchFlipTest, EnsureFetchXorUsed) {
  auto op = with_order{seq_cst, folly::atomic_fetch_flip};

  atomic_fetch_flip_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint64_t>(op);
}

TEST_F(AtomicFetchSetTest, FetchSetFallback) {
  auto op = with_order{seq_cst, folly::detail::atomic_fetch_set_fallback};

  atomic_fetch_set_basic<std::uint16_t>(op);
  atomic_fetch_set_basic<std::uint32_t>(op);
  atomic_fetch_set_basic<std::uint64_t>(op);
  atomic_fetch_set_basic<std::uint8_t>(op);

  atomic_fetch_set_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint64_t>(op);
}

TEST_F(AtomicFetchResetTest, FetchResetFallback) {
  auto op = with_order{seq_cst, folly::detail::atomic_fetch_reset_fallback};

  atomic_fetch_reset_basic<std::uint16_t>(op);
  atomic_fetch_reset_basic<std::uint32_t>(op);
  atomic_fetch_reset_basic<std::uint64_t>(op);
  atomic_fetch_reset_basic<std::uint8_t>(op);

  atomic_fetch_reset_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint64_t>(op);
}

TEST_F(AtomicFetchFlipTest, FetchFlipFallback) {
  auto op = with_order{seq_cst, folly::detail::atomic_fetch_flip_fallback};

  atomic_fetch_flip_basic<std::uint16_t>(op);
  atomic_fetch_flip_basic<std::uint32_t>(op);
  atomic_fetch_flip_basic<std::uint64_t>(op);
  atomic_fetch_flip_basic<std::uint8_t>(op);

  atomic_fetch_flip_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint64_t>(op);
}

TEST_F(AtomicFetchSetTest, FetchSetDefault) {
  auto op = folly::atomic_fetch_set;

  atomic_fetch_set_basic<std::uint16_t>(op);
  atomic_fetch_set_basic<std::uint32_t>(op);
  atomic_fetch_set_basic<std::uint64_t>(op);
  atomic_fetch_set_basic<std::uint8_t>(op);

  atomic_fetch_set_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_set_non_std_atomic<std::uint64_t>(op);
}

TEST_F(AtomicFetchResetTest, FetchResetDefault) {
  auto op = folly::atomic_fetch_reset;

  atomic_fetch_reset_basic<std::uint16_t>(op);
  atomic_fetch_reset_basic<std::uint32_t>(op);
  atomic_fetch_reset_basic<std::uint64_t>(op);
  atomic_fetch_reset_basic<std::uint8_t>(op);

  atomic_fetch_reset_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_reset_non_std_atomic<std::uint64_t>(op);
}

TEST_F(AtomicFetchFlipTest, FetchFlipDefault) {
  auto op = folly::atomic_fetch_flip;

  atomic_fetch_flip_basic<std::uint16_t>(op);
  atomic_fetch_flip_basic<std::uint32_t>(op);
  atomic_fetch_flip_basic<std::uint64_t>(op);
  atomic_fetch_flip_basic<std::uint8_t>(op);

  atomic_fetch_flip_non_std_atomic<std::uint8_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint16_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint32_t>(op);
  atomic_fetch_flip_non_std_atomic<std::uint64_t>(op);
}

} // namespace folly
