/*
 * Copyright 2013-present Facebook, Inc.
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

#include <folly/Memory.h>

#include <type_traits>
#include <utility>

#include <glog/logging.h>

#include <folly/String.h>
#include <folly/memory/Arena.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(aligned_malloc, examples) {
  auto trial = [](size_t align) {
    auto const ptr = aligned_malloc(1, align);
    return (aligned_free(ptr), uintptr_t(ptr));
  };

  if (!kIsSanitize) { // asan allocator raises SIGABRT instead
    EXPECT_EQ(EINVAL, (trial(2), errno)) << "too small";
    EXPECT_EQ(EINVAL, (trial(513), errno)) << "not power of two";
  }

  EXPECT_EQ(0, trial(512) % 512);
  EXPECT_EQ(0, trial(8192) % 8192);
}

TEST(make_unique, compatible_with_std_make_unique) {
  //  HACK: To enforce that `folly::` is imported here.
  to_shared_ptr(std::unique_ptr<std::string>());

  using namespace std;
  make_unique<string>("hello, world");
}

TEST(to_weak_ptr, example) {
  auto s = std::make_shared<int>(17);
  EXPECT_EQ(1, s.use_count());
  EXPECT_EQ(2, (to_weak_ptr(s).lock(), s.use_count())) << "lvalue";
  EXPECT_EQ(3, (to_weak_ptr(decltype(s)(s)).lock(), s.use_count())) << "rvalue";
}

TEST(SysAllocator, equality) {
  using Alloc = SysAllocator<float>;
  Alloc const a, b;
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
}

TEST(SysAllocator, allocate_unique) {
  using Alloc = SysAllocator<float>;
  Alloc const alloc;
  auto ptr = allocate_unique<float>(alloc, 3.);
  EXPECT_EQ(3., *ptr);
}

TEST(SysAllocator, vector) {
  using Alloc = SysAllocator<float>;
  Alloc const alloc;
  std::vector<float, Alloc> nums(alloc);
  nums.push_back(3.);
  nums.push_back(5.);
  EXPECT_THAT(nums, testing::ElementsAreArray({3., 5.}));
}

TEST(SysAllocator, bad_alloc) {
  using Alloc = SysAllocator<float>;
  Alloc const alloc;
  std::vector<float, Alloc> nums(alloc);
  if (!kIsSanitize) {
    EXPECT_THROW(nums.reserve(1ull << 50), std::bad_alloc);
  }
}

TEST(AlignedSysAllocator, equality_fixed) {
  using Alloc = AlignedSysAllocator<float, FixedAlign<1024>>;
  Alloc const a, b;
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
}

TEST(AlignedSysAllocator, allocate_unique_fixed) {
  using Alloc = AlignedSysAllocator<float, FixedAlign<1024>>;
  Alloc const alloc;
  auto ptr = allocate_unique<float>(alloc, 3.);
  EXPECT_EQ(3., *ptr);
  EXPECT_EQ(0, std::uintptr_t(ptr.get()) % 1024);
}

TEST(AlignedSysAllocator, vector_fixed) {
  using Alloc = AlignedSysAllocator<float, FixedAlign<1024>>;
  Alloc const alloc;
  std::vector<float, Alloc> nums(alloc);
  nums.push_back(3.);
  nums.push_back(5.);
  EXPECT_THAT(nums, testing::ElementsAreArray({3., 5.}));
  EXPECT_EQ(0, std::uintptr_t(nums.data()) % 1024);
}

TEST(AlignedSysAllocator, bad_alloc_fixed) {
  using Alloc = AlignedSysAllocator<float, FixedAlign<1024>>;
  Alloc const alloc;
  std::vector<float, Alloc> nums(alloc);
  if (!kIsSanitize) {
    EXPECT_THROW(nums.reserve(1ull << 50), std::bad_alloc);
  }
}

TEST(AlignedSysAllocator, equality_default) {
  using Alloc = AlignedSysAllocator<float>;
  Alloc const a(1024), b(1024), c(512);
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
  EXPECT_FALSE(a == c);
  EXPECT_TRUE(a != c);
}

TEST(AlignedSysAllocator, allocate_unique_default) {
  using Alloc = AlignedSysAllocator<float>;
  Alloc const alloc(1024);
  auto ptr = allocate_unique<float>(alloc, 3.);
  EXPECT_EQ(3., *ptr);
  EXPECT_EQ(0, std::uintptr_t(ptr.get()) % 1024);
}

TEST(AlignedSysAllocator, vector_default) {
  using Alloc = AlignedSysAllocator<float>;
  Alloc const alloc(1024);
  std::vector<float, Alloc> nums(alloc);
  nums.push_back(3.);
  nums.push_back(5.);
  EXPECT_THAT(nums, testing::ElementsAreArray({3., 5.}));
  EXPECT_EQ(0, std::uintptr_t(nums.data()) % 1024);
}

TEST(AlignedSysAllocator, bad_alloc_default) {
  using Alloc = AlignedSysAllocator<float>;
  Alloc const alloc(1024);
  std::vector<float, Alloc> nums(alloc);
  if (!kIsSanitize) {
    EXPECT_THROW(nums.reserve(1ull << 50), std::bad_alloc);
  }
}

TEST(allocate_sys_buffer, compiles) {
  auto buf = allocate_sys_buffer(256);
  //  Freed at the end of the scope.
}

template <typename C>
static void test_enable_shared_from_this(std::shared_ptr<C> sp) {
  ASSERT_EQ(1l, sp.use_count());

  // Test shared_from_this().
  std::shared_ptr<C> sp2 = sp->shared_from_this();
  ASSERT_EQ(sp, sp2);

  // Test weak_from_this().
  std::weak_ptr<C> wp = sp->weak_from_this();
  ASSERT_EQ(sp, wp.lock());
  sp.reset();
  sp2.reset();
  ASSERT_EQ(nullptr, wp.lock());

  // Test shared_from_this() and weak_from_this() on object not owned by a
  // shared_ptr. Undefined in C++14 but well-defined in C++17. Also known to
  // work with libstdc++ >= 20150123. Feel free to add other standard library
  // versions where the behavior is known.
#if __cplusplus >= 201700L || \
    __GLIBCXX__ >= 20150123L
  C stack_resident;
  ASSERT_THROW(stack_resident.shared_from_this(), std::bad_weak_ptr);
  ASSERT_TRUE(stack_resident.weak_from_this().expired());
#endif
}

TEST(enable_shared_from_this, compatible_with_std_enable_shared_from_this) {
  // Compile-time compatibility.
  class C_std : public std::enable_shared_from_this<C_std> {};
  class C_folly : public folly::enable_shared_from_this<C_folly> {};
  static_assert(
    noexcept(std::declval<C_std>().shared_from_this()) ==
    noexcept(std::declval<C_folly>().shared_from_this()), "");
  static_assert(
    noexcept(std::declval<C_std const>().shared_from_this()) ==
    noexcept(std::declval<C_folly const>().shared_from_this()), "");
  static_assert(noexcept(std::declval<C_folly>().weak_from_this()), "");
  static_assert(noexcept(std::declval<C_folly const>().weak_from_this()), "");

  // Runtime compatibility.
  test_enable_shared_from_this(std::make_shared<C_folly>());
  test_enable_shared_from_this(std::make_shared<C_folly const>());
}
