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

#include <folly/memory/ReentrantAllocator.h>

#include <memory>
#include <thread>
#include <tuple>
#include <vector>

#include <folly/Utility.h>
#include <folly/functional/Invoke.h>
#include <folly/portability/GTest.h>

class ReentrantAllocatorTest : public testing::Test {};

TEST_F(ReentrantAllocatorTest, equality) {
  folly::reentrant_allocator<void> a{folly::reentrant_allocator_options{}};
  folly::reentrant_allocator<void> b{folly::reentrant_allocator_options{}};
  EXPECT_TRUE(a == a);
  EXPECT_FALSE(a == b);
  EXPECT_FALSE(a != a);
  EXPECT_TRUE(a != b);

  folly::reentrant_allocator<int> a2{a};
  folly::reentrant_allocator<int> b2{b};
  EXPECT_TRUE(a2 == a);
  EXPECT_FALSE(a2 == b);
  EXPECT_TRUE(a2 == a2);
  EXPECT_FALSE(a2 == b2);
  EXPECT_FALSE(a2 != a);
  EXPECT_TRUE(a2 != b);
  EXPECT_FALSE(a2 != a2);
  EXPECT_TRUE(a2 != b2);
}

TEST_F(ReentrantAllocatorTest, small) {
  folly::reentrant_allocator<void> alloc{folly::reentrant_allocator_options{}};
  std::vector<size_t, folly::reentrant_allocator<size_t>> vec{alloc};
  for (auto i = 0u; i < (1u << 16); ++i) {
    vec.push_back(i);
  }
  for (auto i = 0u; i < (1u << 16); ++i) {
    EXPECT_EQ(i, vec.at(i));
  }
}

TEST_F(ReentrantAllocatorTest, large) {
  constexpr size_t const large_size_lg = 6;
  struct alignas(1u << large_size_lg) type : std::tuple<size_t> {
    using std::tuple<size_t>::tuple;
  };
  auto const opts = folly::reentrant_allocator_options{} //
                        .large_size_lg(large_size_lg);
  folly::reentrant_allocator<void> alloc{opts};
  std::vector<type, folly::reentrant_allocator<type>> vec{alloc};
  for (auto i = 0u; i < (1u << 16); ++i) {
    vec.push_back({i});
  }
  for (auto i = 0u; i < (1u << 16); ++i) {
    EXPECT_EQ(i, std::get<0>(vec.at(i)));
  }
}

TEST_F(ReentrantAllocatorTest, zero) {
  folly::reentrant_allocator<int> a{folly::reentrant_allocator_options{}};
  a.deallocate(nullptr, 0);
  auto ptr = a.allocate(0);
  EXPECT_NE(nullptr, ptr);
  a.deallocate(ptr, 0);
}

TEST_F(ReentrantAllocatorTest, self_assignment) {
  folly::reentrant_allocator<int> a{folly::reentrant_allocator_options{}};
  auto& i = *a.allocate(1);
  ::new (&i) int(7);
  EXPECT_EQ(7, i);
  a = folly::as_const(a);
  EXPECT_EQ(7, i);
  a.deallocate(&i, 1);
}

TEST_F(ReentrantAllocatorTest, stress) {
  struct alignas(256) big {};
  folly::reentrant_allocator<void> a{folly::reentrant_allocator_options{}};
  std::vector<std::thread> threads{4};
  std::atomic<bool> done{false};
  for (auto& th : threads) {
    th = std::thread([&done, a] {
      while (!done.load(std::memory_order_relaxed)) {
        std::allocate_shared<big>(a);
      }
    });
  }
  /* sleep override */ std::this_thread::sleep_for(std::chrono::seconds(1));
  done.store(true, std::memory_order_relaxed);
  for (auto& th : threads) {
    th.join();
  }
}

namespace member_invoker {

namespace {

FOLLY_CREATE_MEMBER_INVOKER(allocate, allocate);
FOLLY_CREATE_MEMBER_INVOKER(deallocate, deallocate);
FOLLY_CREATE_MEMBER_INVOKER(max_size, max_size);
FOLLY_CREATE_MEMBER_INVOKER(address, address);

} // namespace

} // namespace member_invoker

TEST_F(ReentrantAllocatorTest, invocability) {
  namespace m = member_invoker;
  using av = folly::reentrant_allocator<void>;
  using ac = folly::reentrant_allocator<char>;

  EXPECT_FALSE((folly::is_invocable_v<m::allocate, av, size_t>));
  EXPECT_FALSE((folly::is_invocable_v<m::deallocate, av, void*, size_t>));
  EXPECT_FALSE((folly::is_invocable_v<m::max_size, av>));

  EXPECT_TRUE((folly::is_invocable_r_v<char*, m::allocate, ac, size_t>));
  EXPECT_TRUE(
      (folly::is_invocable_r_v<void, m::deallocate, ac, char*, size_t>));
  EXPECT_TRUE((folly::is_invocable_r_v<size_t, m::max_size, ac>));
  EXPECT_TRUE((folly::is_invocable_r_v<char*, m::address, ac, char&>));
  EXPECT_TRUE(
      (folly::is_invocable_r_v<char const*, m::address, ac, char const&>));
}
