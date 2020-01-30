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

#include <folly/synchronization/detail/InlineFunctionRef.h>

#include <folly/portability/GTest.h>

#include <cstring>

namespace folly {
namespace detail {

class InlineFunctionRefTest : public ::testing::Test {
 public:
  template <typename InlineFRef>
  static auto& storage(InlineFRef& fref) {
    return fref.storage_;
  }

  template <typename InlineFRef>
  static auto& call(InlineFRef& fref) {
    return fref.call_;
  }
};

TEST_F(InlineFunctionRefTest, BasicInvoke) {
  {
    auto func = [dummy = int{0}](auto integer) { return integer + dummy; };
    auto copy = func;
    auto fref =
        InlineFunctionRef<int(int), 2 * sizeof(uintptr_t)>{std::move(func)};
    EXPECT_EQ(fref(1), 1);
    EXPECT_EQ(fref(2), 2);
    EXPECT_EQ(fref(3), 3);

    static_assert(sizeof(copy) == sizeof(int), "Make sure no padding");
    EXPECT_EQ(std::memcmp(&storage(fref), &copy, sizeof(copy)), 0);
  }
}

TEST_F(InlineFunctionRefTest, InvokeWithCapture) {
  {
    auto data = std::uint64_t{1};
    auto func = [&](auto integer) { return integer + data; };
    auto copy = func;
    auto fref = InlineFunctionRef<int(int), 24>{std::move(func)};

    EXPECT_EQ(fref(1), 2);
    EXPECT_EQ(fref(2), 3);
    EXPECT_EQ(fref(3), 4);

    data = 2;
    EXPECT_EQ(fref(1), 3);
    EXPECT_EQ(fref(2), 4);
    EXPECT_EQ(fref(3), 5);

    data = 3;
    EXPECT_EQ(fref(1), 4);
    EXPECT_EQ(fref(2), 5);
    EXPECT_EQ(fref(3), 6);

    EXPECT_EQ(sizeof(copy), 8);
    EXPECT_EQ(std::memcmp(&storage(fref), &copy, 8), 0);
  }
}

TEST_F(InlineFunctionRefTest, InvokeWithFunctionPointer) {
  {
    using FPtr = int (*)(int);
    auto func = FPtr{[](auto integer) { return integer; }};
    auto copy = func;

    // we move into InlineFunctionRef but the move doesn't actually do anything
    // destructive to the parameter
    auto fref = InlineFunctionRef<int(int), 24>{std::move(func)};

    EXPECT_EQ(fref(1), 1);
    EXPECT_EQ(fref(2), 2);
    EXPECT_EQ(fref(3), 3);

    EXPECT_EQ(sizeof(func), 8);
    EXPECT_EQ(std::memcmp(&storage(fref), &copy, 8), 0);
  }
}

TEST_F(InlineFunctionRefTest, InvokeWithBigLambda) {
  {
    auto data = std::array<std::uint8_t, 128>{};
    for (auto i = std::size_t{0}; i < data.size(); ++i) {
      data[i] = i;
    }
    auto func = [data](auto integer) { return integer + data[integer]; };
    auto copy = func;
    auto address = &func;
    auto fref = InlineFunctionRef<int(int), 24>{std::move(func)};

    EXPECT_EQ(fref(1), 2);
    EXPECT_EQ(fref(2), 4);
    EXPECT_EQ(fref(3), 6);

    EXPECT_EQ(sizeof(copy), 128);
    EXPECT_EQ(sizeof(copy), sizeof(func));
    EXPECT_EQ(std::memcmp(&storage(fref), &address, 8), 0);
    EXPECT_EQ(std::memcmp(&copy, &func, sizeof(copy)), 0);
  }
}

TEST_F(InlineFunctionRefTest, Nullability) {
  auto fref = InlineFunctionRef<void(), 24>{nullptr};
  EXPECT_FALSE(fref);
}

TEST_F(InlineFunctionRefTest, CopyConstruction) {
  {
    auto data = std::uint64_t{1};
    auto func = [&](auto integer) { return integer + data; };
    auto one = InlineFunctionRef<int(int), 24>{std::move(func)};
    auto two = one;
    EXPECT_EQ(std::memcmp(&one, &two, sizeof(one)), 0);

    EXPECT_EQ(two(1), 2);
    EXPECT_EQ(two(2), 3);
    EXPECT_EQ(two(3), 4);

    data = 2;
    EXPECT_EQ(two(1), 3);
    EXPECT_EQ(two(2), 4);
    EXPECT_EQ(two(3), 5);

    data = 3;
    EXPECT_EQ(two(1), 4);
    EXPECT_EQ(two(2), 5);
    EXPECT_EQ(two(3), 6);
  }

  {
    auto data = std::array<std::uint8_t, 128>{};
    for (auto i = std::size_t{0}; i < data.size(); ++i) {
      data[i] = i;
    }
    auto func = [data](auto integer) { return integer + data[integer]; };
    auto one = InlineFunctionRef<int(int), 24>{std::move(func)};
    auto two = one;
    EXPECT_EQ(std::memcmp(&one, &two, sizeof(one)), 0);

    EXPECT_EQ(two(1), 2);
    EXPECT_EQ(two(2), 4);
    EXPECT_EQ(two(3), 6);

    EXPECT_EQ(sizeof(func), 128);
    auto address = &func;
    EXPECT_EQ(std::memcmp(&storage(two), &address, 8), 0);
  }
}

TEST_F(InlineFunctionRefTest, TestTriviality) {
  {
    auto lambda = []() {};
    auto fref = InlineFunctionRef<void(), 24>{std::move(lambda)};
    EXPECT_TRUE(std::is_trivially_destructible<decltype(fref)>{});
    EXPECT_TRUE(folly::is_trivially_copyable<decltype(fref)>{});
  }
  {
    auto integer = std::uint64_t{0};
    auto lambda = [&]() { static_cast<void>(integer); };
    auto fref = InlineFunctionRef<void(), 24>{std::move(lambda)};
    EXPECT_TRUE(std::is_trivially_destructible<decltype(fref)>{});
    EXPECT_TRUE(folly::is_trivially_copyable<decltype(fref)>{});
  }
  {
    auto data = std::array<std::uint8_t, 128>{};
    auto lambda = [data]() { static_cast<void>(data); };
    auto fref = InlineFunctionRef<void(), 24>{std::move(lambda)};
    EXPECT_TRUE(std::is_trivially_destructible<decltype(fref)>{});
    EXPECT_TRUE(folly::is_trivially_copyable<decltype(fref)>{});
  }
}

namespace {
template <typename Data>
class ConstQualifiedFunctor {
 public:
  int operator()() {
    return 0;
  }
  int operator()() const {
    return 1;
  }

  Data data_;
};
} // namespace

TEST_F(InlineFunctionRefTest, CallConstQualifiedMethod) {
  {
    auto small = ConstQualifiedFunctor<std::uint8_t>{};
    auto fref = InlineFunctionRef<int(), 24>{std::move(small)};
    EXPECT_EQ(fref(), 1);
  }
  {
    const auto small = ConstQualifiedFunctor<std::uint8_t>{};
    auto fref = InlineFunctionRef<int(), 24>{std::move(small)};
    EXPECT_EQ(fref(), 1);
  }
  {
    auto big = ConstQualifiedFunctor<std::array<std::uint8_t, 128>>{};
    auto fref = InlineFunctionRef<int(), 24>{std::move(big)};
    EXPECT_EQ(fref(), 1);
  }
  {
    const auto big = ConstQualifiedFunctor<std::array<std::uint8_t, 128>>{};
    auto fref = InlineFunctionRef<int(), 24>{std::move(big)};
    EXPECT_EQ(fref(), 1);
  }
}

namespace {
template <std::size_t Size>
using InlineFRef = InlineFunctionRef<void(), Size>;
} // namespace

TEST_F(InlineFunctionRefTest, TestSizeAlignment) {
  EXPECT_EQ(sizeof(storage(std::declval<InlineFRef<16>&>())), 8);
  EXPECT_EQ(alignof(decltype(storage(std::declval<InlineFRef<16>&>()))), 8);

  EXPECT_EQ(sizeof(storage(std::declval<InlineFRef<24>&>())), 16);
  EXPECT_EQ(alignof(decltype(storage(std::declval<InlineFRef<24>&>()))), 8);

  EXPECT_EQ(sizeof(storage(std::declval<InlineFRef<32>&>())), 24);
  EXPECT_EQ(alignof(decltype(storage(std::declval<InlineFRef<32>&>()))), 8);
}

namespace {
int foo(int integer) {
  return integer;
}
} // namespace

TEST_F(InlineFunctionRefTest, TestFunctionPointer) {
  auto fref = InlineFunctionRef<int(int), 24>{&foo};
  EXPECT_EQ(fref(1), 1);
  EXPECT_EQ(fref(2), 2);
  EXPECT_EQ(fref(3), 3);
}

namespace {
class alignas(16) MaxAligned {};
class alignas(32) MoreThanMaxAligned {};
} // namespace

TEST_F(InlineFunctionRefTest, TestMaxAlignment) {
  {
    auto aligned = MaxAligned{};
    auto func = [aligned]() { static_cast<void>(aligned); };
    auto fref = InlineFunctionRef<void(), 24>{std::move(func)};
    auto address = &func;
    EXPECT_EQ(std::memcmp(&storage(fref), &address, 8), 0);
  }
  {
    auto aligned = MoreThanMaxAligned{};
    auto func = [aligned]() { static_cast<void>(aligned); };
    auto fref = InlineFunctionRef<void(), 24>{std::move(func)};
    auto address = &func;
    EXPECT_EQ(std::memcmp(&storage(fref), &address, 8), 0);
  }
}

TEST_F(InlineFunctionRefTest, TestLValueConstructibility) {
  auto lambda = []() {};
  EXPECT_TRUE((!std::is_constructible<
               InlineFunctionRef<void(), 24>,
               decltype(lambda)&>{}));
}

} // namespace detail
} // namespace folly
