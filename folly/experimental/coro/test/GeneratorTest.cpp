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

#include <folly/Portability.h>

#include <algorithm>

#include <folly/ScopeGuard.h>
#include <folly/experimental/coro/Generator.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

class GeneratorTest : public testing::Test {};

TEST_F(GeneratorTest, DefaultConstructed_EmptySequence) {
  Generator<std::uint32_t> ints;
  EXPECT_EQ(ints.begin(), ints.end());
}

TEST_F(GeneratorTest, NonRecursiveUse) {
  auto f = []() -> Generator<float> {
    co_yield 1.0f;
    co_yield 2.0f;
  };

  auto gen = f();
  auto iter = gen.begin();
  EXPECT_EQ(*iter, 1.0f);
  ++iter;
  EXPECT_EQ(*iter, 2.0f);
  ++iter;
  EXPECT_EQ(iter, gen.end());
}

TEST_F(GeneratorTest, ThrowsBeforeYieldingFirstElement_RethrowsFromBegin) {
  class MyException : public std::exception {};

  auto f = []() -> Generator<std::uint32_t> {
    throw MyException{};
    co_return;
  };

  auto gen = f();
  EXPECT_THROW(gen.begin(), MyException);
}

TEST_F(GeneratorTest, ThrowsAfterYieldingFirstElement_RethrowsFromIncrement) {
  class MyException : public std::exception {};

  auto f = []() -> Generator<std::uint32_t> {
    co_yield 1;
    throw MyException{};
  };

  auto gen = f();
  auto iter = gen.begin();
  EXPECT_EQ(*iter, 1u);
  EXPECT_THROW(++iter, MyException);
}

TEST_F(GeneratorTest, NotStartedUntilCalled) {
  bool reachedA = false;
  bool reachedB = false;
  bool reachedC = false;
  auto f = [&]() -> Generator<std::uint32_t> {
    reachedA = true;
    co_yield 1;
    reachedB = true;
    co_yield 2;
    reachedC = true;
  };

  auto gen = f();
  EXPECT_FALSE(reachedA);
  auto iter = gen.begin();
  EXPECT_TRUE(reachedA);
  EXPECT_FALSE(reachedB);
  EXPECT_EQ(*iter, 1u);
  ++iter;
  EXPECT_TRUE(reachedB);
  EXPECT_FALSE(reachedC);
  EXPECT_EQ(*iter, 2u);
  ++iter;
  EXPECT_TRUE(reachedC);
  EXPECT_EQ(iter, gen.end());
}

TEST_F(GeneratorTest, DestroyedBeforeCompletion_DestructsObjectsOnStack) {
  bool destructed = false;
  bool completed = false;
  auto f = [&]() -> Generator<std::uint32_t> {
    SCOPE_EXIT { destructed = true; };

    co_yield 1;
    co_yield 2;
    completed = true;
  };

  {
    auto g = f();
    auto it = g.begin();
    auto itEnd = g.end();
    EXPECT_NE(it, itEnd);
    EXPECT_EQ(*it, 1u);
    EXPECT_FALSE(destructed);
  }

  EXPECT_FALSE(completed);
  EXPECT_TRUE(destructed);
}

TEST_F(GeneratorTest, SimpleRecursiveYield) {
  auto f = [](int n, auto& f_) -> Generator<const std::uint32_t> {
    co_yield n;
    if (n > 0) {
      co_yield f_(n - 1, f_);
      co_yield n;
    }
  };

  auto f2 = [&f](int n) { return f(n, f); };

  {
    auto gen = f2(1);
    auto iter = gen.begin();
    EXPECT_EQ(*iter, 1u);
    ++iter;
    EXPECT_EQ(*iter, 0u);
    ++iter;
    EXPECT_EQ(*iter, 1u);
    ++iter;
    EXPECT_EQ(iter, gen.end());
  }

  {
    auto gen = f2(2);
    auto iter = gen.begin();
    EXPECT_EQ(*iter, 2u);
    ++iter;
    EXPECT_EQ(*iter, 1u);
    ++iter;
    EXPECT_EQ(*iter, 0u);
    ++iter;
    EXPECT_EQ(*iter, 1u);
    ++iter;
    EXPECT_EQ(*iter, 2u);
    ++iter;
    EXPECT_EQ(iter, gen.end());
  }
}

TEST_F(GeneratorTest, NestedEmptyYield) {
  auto f = []() -> Generator<std::uint32_t> { co_return; };

  auto g = [&f]() -> Generator<std::uint32_t> {
    co_yield 1;
    co_yield f();
    co_yield 2;
  };

  auto gen = g();
  auto iter = gen.begin();
  EXPECT_EQ(*iter, 1u);
  ++iter;
  EXPECT_EQ(*iter, 2u);
  ++iter;
  EXPECT_EQ(iter, gen.end());
}

TEST_F(GeneratorTest, ExceptionThrownFromRecursiveCall_CanBeCaughtByCaller) {
  class SomeException : public std::exception {};
  bool caught = false;

  auto f = [&](std::uint32_t depth, auto&& f_) -> Generator<std::uint32_t> {
    if (depth == 1u) {
      throw SomeException{};
    }

    co_yield 1;

    try {
      co_yield f_(1, f_);
    } catch (const SomeException&) {
      caught = true;
    }

    co_yield 2;
  };

  auto gen = f(0, f);
  auto iter = gen.begin();
  EXPECT_EQ(*iter, 1u);
  EXPECT_FALSE(caught);
  ++iter;
  EXPECT_TRUE(caught);
  EXPECT_EQ(*iter, 2u);
  ++iter;
  EXPECT_EQ(iter, gen.end());
}

TEST_F(GeneratorTest, ExceptionThrownFromNestedCall_CanBeCaughtByCaller) {
  class SomeException : public std::exception {};

  auto f = [](std::uint32_t depth, auto&& f_) -> Generator<std::uint32_t> {
    if (depth == 4u) {
      throw SomeException{};
    } else if (depth == 3u) {
      co_yield 3;

      bool caught = false;
      try {
        co_yield f_(4, f_);
      } catch (const SomeException&) {
        caught = true;
      }

      co_yield caught ? 33 : 1337;

      throw SomeException{};
    } else if (depth == 2u) {
      bool caught = false;
      try {
        co_yield f_(3, f_);
      } catch (const SomeException&) {
        caught = true;
      }

      if (caught) {
        co_yield 2;
      }
    } else {
      co_yield 1;
      co_yield f_(2, f_);
      co_yield f_(3, f_);
    }
  };

  auto gen = f(1, f);
  auto iter = gen.begin();
  EXPECT_EQ(*iter, 1u);
  ++iter;
  EXPECT_EQ(*iter, 3u);
  ++iter;
  EXPECT_EQ(*iter, 33u);
  ++iter;
  EXPECT_EQ(*iter, 2u);
  ++iter;
  EXPECT_EQ(*iter, 3u);
  ++iter;
  EXPECT_EQ(*iter, 33u);
  EXPECT_THROW(++iter, SomeException);

  EXPECT_EQ(iter, gen.end());
}

namespace {
Generator<std::uint32_t> iterate_range(std::uint32_t begin, std::uint32_t end) {
  if ((end - begin) <= 10u) {
    for (std::uint32_t i = begin; i < end; ++i) {
      co_yield i;
    }
  } else {
    std::uint32_t mid = begin + (end - begin) / 2;
    co_yield iterate_range(begin, mid);
    co_yield iterate_range(mid, end);
  }
}
} // namespace

TEST_F(GeneratorTest, UsageInStandardAlgorithms) {
  {
    auto a = iterate_range(5, 30);
    auto b = iterate_range(5, 30);
    EXPECT_TRUE(std::equal(a.begin(), a.end(), b.begin(), b.end()));
  }

  {
    auto a = iterate_range(5, 30);
    auto b = iterate_range(5, 300);
    EXPECT_FALSE(std::equal(a.begin(), a.end(), b.begin(), b.end()));
  }
}

TEST_F(GeneratorTest, InvokeLambda) {
  auto ptr = std::make_unique<int>(123);
  auto gen = folly::coro::co_invoke(
      [p = std::move(
           ptr)]() mutable -> folly::coro::Generator<std::unique_ptr<int>&&> {
        co_yield std::move(p);
      });

  auto it = gen.begin();
  auto result = std::move(*it);
  EXPECT_NE(result, nullptr);
  EXPECT_EQ(*result, 123);
}
} // namespace coro
} // namespace folly

#endif
