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

#include <folly/synchronization/DelayedInit.h>

#include <ostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <fmt/ostream.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/test/Barrier.h>

namespace folly {

namespace {
struct CtorCounts {
  int ctor;
  int copy;
  int move;

  CtorCounts() : CtorCounts(0, 0, 0) {}
  explicit CtorCounts(int ctor, int copy, int move)
      : ctor(ctor), copy(copy), move(move) {}

  void reset() { *this = CtorCounts{}; }

  struct Tracker {
    int value;

    explicit Tracker(CtorCounts& counts, int value = 0)
        : value(value), counts_(counts) {
      ++counts_.ctor;
    }
    Tracker(const Tracker& other) : value(other.value), counts_(other.counts_) {
      ++counts_.copy;
    }
    Tracker(Tracker&& other) noexcept
        : value(other.value), counts_(other.counts_) {
      ++counts_.move;
    }
    Tracker& operator=(const Tracker&) = delete;
    Tracker& operator=(Tracker&&) = delete;

   private:
    CtorCounts& counts_;
  };
};

bool operator==(const CtorCounts& lhs, const CtorCounts& rhs) {
  return std::tie(lhs.ctor, lhs.copy, lhs.move) ==
      std::tie(rhs.ctor, rhs.copy, rhs.move);
}

std::ostream& operator<<(std::ostream& out, const CtorCounts& counts) {
  fmt::print(
      out, "CtorCounts({}, {}, {})", counts.ctor, counts.copy, counts.move);
  return out;
}
} // namespace

TEST(DelayedInit, Simple) {
  CtorCounts counts;
  DelayedInit<CtorCounts::Tracker> lazy;

  for (int i = 0; i < 100; ++i) {
    if (i > 50) {
      auto& tracker = lazy.try_emplace(counts, 12);
      ASSERT_EQ(tracker.value, 12);
      ASSERT_EQ(counts, CtorCounts(1, 0, 0));
      ASSERT_TRUE(lazy.has_value());
    } else {
      ASSERT_EQ(counts, CtorCounts(0, 0, 0));
      ASSERT_FALSE(lazy.has_value());
    }
  }
}

TEST(DelayedInit, TryEmplaceWithPreservesValueCategory) {
  CtorCounts counts;
  CtorCounts::Tracker tracker{counts, 12};

  {
    counts.reset();
    DelayedInit<CtorCounts::Tracker> lazy;
    auto& trackerCopied = lazy.try_emplace_with(
        [&]() -> const CtorCounts::Tracker& { return tracker; });
    EXPECT_EQ(counts, CtorCounts(0, 1, 0));
    EXPECT_EQ(trackerCopied.value, 12);
  }

  {
    counts.reset();
    DelayedInit<CtorCounts::Tracker> lazy;
    auto& trackerMoved = lazy.try_emplace_with(
        [&]() -> CtorCounts::Tracker&& { return std::move(tracker); });
    EXPECT_EQ(counts, CtorCounts(0, 0, 1));
    EXPECT_EQ(trackerMoved.value, 12);
  }
}

TEST(DelayedInit, TryEmplacePreservesValueCategory) {
  CtorCounts counts;
  CtorCounts::Tracker tracker{counts, 12};

  {
    counts.reset();
    DelayedInit<CtorCounts::Tracker> lazy;
    auto& trackerCopied = lazy.try_emplace(tracker);
    EXPECT_EQ(counts, CtorCounts(0, 1, 0));
    EXPECT_EQ(trackerCopied.value, 12);
  }

  {
    counts.reset();
    DelayedInit<CtorCounts::Tracker> lazy;
    auto& trackerMoved = lazy.try_emplace(std::move(tracker));
    EXPECT_EQ(counts, CtorCounts(0, 0, 1));
    EXPECT_EQ(trackerMoved.value, 12);
  }
}

TEST(DelayedInit, TryEmplaceWithInitializerList) {
  struct Thing {
    explicit Thing(std::initializer_list<int> ilist) : ilist(ilist) {}
    std::initializer_list<int> ilist;
  };

  DelayedInit<Thing> lazy;
  auto& thing = lazy.try_emplace({1, 2, 3});
  EXPECT_EQ(thing.ilist.size(), 3);

  DelayedInit<const Thing> constLazy;
  auto& constThing = constLazy.try_emplace({1, 2, 3});
  EXPECT_EQ(constThing.ilist.size(), 3);
}

TEST(DelayedInit, Value) {
  DelayedInit<int> lazy;

  EXPECT_THROW(lazy.value(), std::logic_error);
  lazy.try_emplace_with([] { return 0; });
  EXPECT_EQ(lazy.value(), 0);
}

TEST(DelayedInit, CalledOnce) {
  CtorCounts counts;
  DelayedInit<CtorCounts::Tracker> lazy;

  auto& tracker1 =
      lazy.try_emplace_with([&]() { return CtorCounts::Tracker(counts, 1); });
  auto& tracker2 =
      lazy.try_emplace_with([&]() { return CtorCounts::Tracker(counts, 2); });

  EXPECT_EQ(counts, CtorCounts(1, 0, 0));
  EXPECT_EQ(tracker1.value, 1);
  EXPECT_EQ(tracker2.value, 1);
}

TEST(DelayedInit, ConvertibleConstruction) {
  CtorCounts counts;
  DelayedInit<CtorCounts::Tracker> lazy;

  auto& tracker = lazy.try_emplace(counts);
  EXPECT_EQ(tracker.value, 0);
}

TEST(DelayedInit, Destructor) {
  struct Thing {
    explicit Thing(bool& destroyed) : destroyed_(destroyed) {}
    ~Thing() noexcept { destroyed_ = true; }

   private:
    bool& destroyed_;
  };

  bool destroyed = false;
  {
    DelayedInit<Thing> lazy;
    lazy.try_emplace(destroyed);
  }
  EXPECT_TRUE(destroyed);
}

TEST(DelayedInit, ExceptionProof) {
  DelayedInit<int> lazy;

  EXPECT_THROW(
      lazy.try_emplace_with([]() -> int { throw std::exception{}; }),
      std::exception);
  EXPECT_FALSE(lazy.has_value());

  auto& value = lazy.try_emplace(1);
  EXPECT_EQ(value, 1);
  ASSERT_TRUE(lazy.has_value());

  EXPECT_EQ(*lazy, 1);
  value = lazy.try_emplace_with([]() -> int { throw std::exception{}; });
  EXPECT_EQ(value, 1);
}

TEST(DelayedInit, ConstType) {
  CtorCounts counts;
  DelayedInit<const CtorCounts::Tracker> lazy;
  EXPECT_FALSE(lazy.has_value());

  auto& tracker = lazy.try_emplace(counts, 12);
  EXPECT_EQ(tracker.value, 12);
  EXPECT_TRUE(lazy.has_value());
  EXPECT_TRUE(
      (std::is_same<decltype(tracker), const CtorCounts::Tracker&>::value));

  EXPECT_TRUE((
      std::is_same<decltype(lazy.value()), const CtorCounts::Tracker&>::value));
  EXPECT_EQ(lazy.value().value, 12);

  EXPECT_TRUE(
      (std::is_same<decltype(*lazy), const CtorCounts::Tracker&>::value));
  EXPECT_EQ(lazy->value, 12);
}

namespace {
template <typename T>
class DelayedInitSizeTest : public testing::Test {};

template <typename T>
struct WithOneByte {
  T t;
  char c;
};
} // namespace

using DelayedInitSizeTestTypes =
    testing::Types<char, short, int, long, long long, char[3], short[2]>;
TYPED_TEST_SUITE(DelayedInitSizeTest, DelayedInitSizeTestTypes);

TYPED_TEST(DelayedInitSizeTest, Size) {
  // DelayedInit should not add more than 1-byte size overhead (modulo padding)
  EXPECT_EQ(sizeof(DelayedInit<TypeParam>), sizeof(WithOneByte<TypeParam>));
}

TEST(DelayedInit, Concurrent) {
  CtorCounts counts;
  DelayedInit<CtorCounts::Tracker> lazy;

  constexpr int N_THREADS = 100;

  folly::test::Barrier barrier{N_THREADS + 1};

  std::vector<std::thread> threads;
  for (int i = 0; i < N_THREADS; ++i) {
    threads.emplace_back([&, value = i] {
      barrier.wait();
      lazy.try_emplace(counts, value);
    });
  }

  barrier.wait();
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(counts, CtorCounts(1, 0, 0));
  EXPECT_TRUE(lazy.has_value());
}

} // namespace folly
