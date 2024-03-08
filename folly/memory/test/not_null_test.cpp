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

#include <folly/memory/not_null.h>

#include <memory>
#include <strstream>
#include <unordered_set>

#include <folly/portability/GTest.h>

using namespace folly;

struct NotNullTest : testing::Test {};
struct NotNullHelperTest : testing::Test {};
struct NotNullUniquePtrTest : testing::Test {};
struct NotNullSharedPtrTest : testing::Test {};

namespace {
struct Base {
  virtual ~Base() = default;
};
struct Derived : public Base {};
struct Derived2 : public Base {};

struct my_deleter {
  explicit my_deleter(int* counter) : counter_(counter) {}
  void operator()(int* ptr) {
    (*counter_)++;
    delete ptr;
  }
  int* counter_;
};
} // namespace

template <typename PtrT>
void nullify(not_null<PtrT>& nn) {
  // Super bad practice, but useful for testing.
  const_cast<PtrT&>(nn.unwrap()) = nullptr;
}

template <typename T>
struct Wrapper {
  template <typename U>
  /* implicit */ Wrapper(U&& u) : t(std::forward<U>(u)) {}

  T t;
};

TEST_F(NotNullTest, is_not_null) {
  static_assert(detail::is_not_null_v<void> == false, "is_not_null failure");
  static_assert(detail::is_not_null_v<int> == false, "is_not_null failure");
  static_assert(detail::is_not_null_v<int*> == false, "is_not_null failure");
  static_assert(
      detail::is_not_null_v<const int* const> == false, "is_not_null failure");
  static_assert(
      detail::is_not_null_v<not_null<int*>> == true, "is_not_null failure");
}

template <typename To, typename From>
bool ctor_throws(From&& from) {
  try {
    not_null<To> nn(std::forward<From>(from));
    EXPECT_TRUE(nn);
  } catch (std::invalid_argument&) {
    return true;
  }
  return false;
}

TEST_F(NotNullTest, ctor_exception) {
  Derived* dp = nullptr;

  Derived d;
  not_null<Derived*> nnd(&d);
  nullify(nnd);

  EXPECT_TRUE(ctor_throws<Derived*>(dp));
  EXPECT_TRUE(ctor_throws<Base*>(dp));
  EXPECT_TRUE(ctor_throws<Base*>(std::move(dp)));

  // Copy/move constructor never throws
  EXPECT_FALSE(ctor_throws<Derived*>(nnd));
  EXPECT_FALSE((ctor_throws<Derived*, const not_null<Derived*>&>(nnd)));

  // Converting constructor fails in debug mode
#ifndef NDEBUG
  EXPECT_DEATH(not_null<Base*> nb1(nnd), "not_null internal pointer is null");
  EXPECT_DEATH(
      not_null<Base*> nb2(std::move(nnd)), "not_null internal pointer is null");
#else
  EXPECT_FALSE(ctor_throws<Base*>(nnd));
  EXPECT_FALSE(ctor_throws<Base*>(std::move(nnd)));
#endif
}

TEST_F(NotNullTest, ctor_conversion) {
  int* p = new int(7);
  not_null_unique_ptr<int> a(p);
  not_null_shared_ptr<int> b(std::move(a));
  EXPECT_EQ(*b, 7);
}

TEST_F(NotNullTest, explicit_construction) {
  int* i = new int(7);

  // Can explicitly construct a unique_ptr from a raw pointer...
  not_null_unique_ptr<int> a(i);
  EXPECT_EQ(*a, 7);
  // ...but not implicitly; the following code does not (and should not)
  // compile.
#if 0
  int* j = new int(8);
  auto f = [](not_null_unique_ptr<int>) {};
  f(j);
#endif
}

TEST_F(NotNullTest, dereferencing) {
  not_null_unique_ptr<std::vector<int>> nn =
      std::make_unique<std::vector<int>>();

  nn->push_back(2);
  EXPECT_EQ((*nn)[0], 2);
}

TEST_F(NotNullTest, bool_cast) {
  int i = 7;
  not_null<int*> nn(&i);
  if (nn) {
  } else {
    EXPECT_FALSE(true);
  }
  if (!nn) {
    EXPECT_FALSE(true);
  }
}

TEST_F(NotNullTest, ptr_casting) {
  int i = 7;
  not_null<int*> nn(&i);
  auto f = [](int* p) { *p = 8; };
  f(nn);
  EXPECT_EQ(i, 8);
}

TEST_F(NotNullTest, conversion_casting) {
  Derived d;
  not_null<Derived*> nnd(&d);
  auto f = [&](Base* b) { EXPECT_EQ(&d, b); };
  f(nnd);
}

TEST_F(NotNullTest, move_casting) {
  not_null<std::unique_ptr<Derived>> nnd = std::make_unique<Derived>();
  auto f = [](std::unique_ptr<Base>) {};
  f(std::move(nnd));

#ifdef NDEBUG
  // use-after-move is disallowed, but possible
  EXPECT_EQ(nnd.unwrap(), nullptr);
#else
  EXPECT_DEATH(nnd.unwrap(), "not_null internal pointer is null");
#endif
}

// If there are multiple ways to convert, not_null's casting operator should
// yield. If the casting operator did not yield, then Wrapper could either be
// implicitly constructed from not_null, or not_null could be casted to Wrapper,
// leading to a compiler error for ambiguity.
TEST_F(NotNullTest, multi_cast) {
  int i = 5;
  not_null<int*> nn(&i);
  auto f = [](Wrapper<not_null<int*>> w) { EXPECT_EQ(*w.t, 5); };

  const auto& cnn = nn;
  f(cnn);

  f(std::move(nn));
}

TEST_F(NotNullTest, unwrap) {
  not_null_unique_ptr<int> nn = std::make_unique<int>(7);
  auto f = [](const std::unique_ptr<int>& u_i) { return *u_i; };
  auto g = [](std::unique_ptr<int>&& u_i) { return *u_i; };
  EXPECT_EQ(f(nn.unwrap()), 7);
  EXPECT_EQ(g(std::move(nn).unwrap()), 7);

  // Because g accepts an rvalue-reference, rather than a value, and does not
  // move-from the argument, nn.unwrap() is still valid.
  EXPECT_NE(nn.unwrap(), nullptr);
}

/**
 * not_null_unique_ptr API tests
 */
TEST_F(NotNullUniquePtrTest, deleter) {
  int counter = 0;
  {
    not_null_unique_ptr<int, my_deleter> a(new int(5), my_deleter(&counter));
    auto d = a.get_deleter();
    EXPECT_EQ(d.counter_, &counter);
  }
  EXPECT_EQ(counter, 1);
}

TEST_F(NotNullUniquePtrTest, reset) {
  not_null_unique_ptr<int> a(new int(5));
  int* i = new int(6);

  a.reset(i);
  EXPECT_EQ(*a, 6);
}

TEST_F(NotNullUniquePtrTest, swap) {
  not_null_unique_ptr<int> a(new int(5));
  not_null_unique_ptr<int> b(new int(6));

  a.swap(b);
  EXPECT_EQ(*a, 6);
  EXPECT_EQ(*b, 5);
}

TEST_F(NotNullUniquePtrTest, get) {
  int* i = new int(5);
  not_null_unique_ptr<int> a(i);
  int* p = a.get();
  EXPECT_EQ(p, i);

  auto g = a.get();
  static_assert(
      detail::is_not_null_v<decltype(g)>, "get() does not return not_null");
}

TEST_F(NotNullUniquePtrTest, assignment) {
  not_null_unique_ptr<int> nnup(new int(5));
  auto up = std::make_unique<int>(6);

  nnup = std::move(up);
  EXPECT_EQ(*nnup, 6);
}

/**
 * not_null_shared_ptr API tests
 */
TEST_F(NotNullSharedPtrTest, deleter) {
  int counter = 0;
  {
    not_null_shared_ptr<int> nnsp(new int(5), my_deleter(&counter));

    auto* deleter = get_deleter<my_deleter>(nnsp);
    EXPECT_EQ(deleter->counter_, &counter);
  }
  EXPECT_EQ(counter, 1);

  // Also test that the first argument can be a not_null pointer.
  {
    not_null<int*> nnp(new int(6));
    not_null_shared_ptr<int> nnsp(nnp, my_deleter(&counter));
    auto* deleter = get_deleter<my_deleter>(nnsp);
    EXPECT_EQ(deleter->counter_, &counter);
  }
  EXPECT_EQ(counter, 2);
}

TEST_F(NotNullSharedPtrTest, aliasing) {
  auto sp = std::make_shared<int>(5);
  int i = 6;
  not_null_shared_ptr<int> nnsp1(sp, &i);
  int j = 7;
  not_null_shared_ptr<int> nnsp2(nnsp1, &j);

  EXPECT_EQ(*sp, 5);
  EXPECT_EQ(*nnsp1, 6);
  EXPECT_EQ(*nnsp2, 7);
  EXPECT_EQ(sp.use_count(), 3);
  EXPECT_EQ(nnsp1.use_count(), 3);
  EXPECT_EQ(nnsp2.use_count(), 3);

  // The move-aliasing-constructor is a c++20 API, and falls back to the const&
  // API without c++20 support. Cannot therefore test use_count().
  not_null_shared_ptr<int> nnsp3(std::move(sp), &i);
  EXPECT_EQ(*nnsp3, 6);
  not_null_shared_ptr<int> nnsp4(std::move(nnsp1), &j);
  EXPECT_EQ(*nnsp4, 7);
}

TEST_F(NotNullSharedPtrTest, null_aliasing) {
  int* i = new int(5);
  int* j = new int(6);
  std::shared_ptr<int> sp1;
  std::shared_ptr<int> sp2(sp1, i);
  ASSERT_NE(sp2.get(), nullptr);
  EXPECT_EQ(*sp2, 5);
  EXPECT_EQ(sp2.use_count(), 0);

  not_null_shared_ptr<int> nnsp(sp1, j);
  EXPECT_EQ(*nnsp, 6);
  EXPECT_EQ(nnsp.use_count(), 0);

  // Null-aliased pointers do not get deleted.
  delete j;
  delete i;
}

TEST_F(NotNullSharedPtrTest, assignment) {
  not_null_shared_ptr<int> nnsp(new int(5));
  auto& ret = nnsp = std::make_unique<int>(6);
  EXPECT_EQ(*nnsp, 6);
  static_assert(
      std::is_same<decltype(ret), not_null_shared_ptr<int>&>::value,
      "operator= wrong return type");
}

TEST_F(NotNullSharedPtrTest, reset) {
  not_null_shared_ptr<int> nnsp(new int(5));
  not_null<int*> nnp1(new int(6));

  nnsp.reset(nnp1);
  EXPECT_EQ(*nnsp, 6);

  int* n = nullptr;
  EXPECT_THROW(nnsp.reset(n), std::invalid_argument);
  EXPECT_EQ(*nnsp, 6); // Strong exception guarantee.

  int counter = 0;
  nnsp.reset(new int(7), my_deleter(&counter));
  EXPECT_EQ(*nnsp, 7);
  not_null<int*> nnp2(new int(8));
  nnsp.reset(nnp2);
  EXPECT_EQ(counter, 1);
}

TEST_F(NotNullSharedPtrTest, swap) {
  not_null_shared_ptr<int> a(new int(5));
  not_null_shared_ptr<int> b(new int(6));

  a.swap(b);
  EXPECT_EQ(*a, 6);
  EXPECT_EQ(*b, 5);
}

TEST_F(NotNullSharedPtrTest, get) {
  not_null_shared_ptr<int> nnsp(new int(5));
  auto p = nnsp.get();
  EXPECT_EQ(*p, 5);
  EXPECT_EQ(*nnsp, 5);
  static_assert(
      std::is_same_v<decltype(p), not_null<int*>>, "wrong return type");
}

TEST_F(NotNullSharedPtrTest, use_count) {
  not_null_shared_ptr<int> nnsp1(new int(5));
  EXPECT_EQ(nnsp1.use_count(), 1);
  {
    not_null_shared_ptr<int> nnsp2(nnsp1);
    EXPECT_EQ(nnsp1.use_count(), 2);
    EXPECT_EQ(nnsp2.use_count(), 2);
  }
  EXPECT_EQ(nnsp1.use_count(), 1);
}

TEST_F(NotNullSharedPtrTest, owner_before) {
  not_null_shared_ptr<int> nnsp1(new int(5));
  not_null_shared_ptr<int> nnsp2(new int(6));
  auto sp = std::make_shared<int>(7);

  auto f = [](const auto& p1, const auto& p2, bool same) {
    bool cmp1 = p1.owner_before(p2);
    bool cmp2 = p2.owner_before(p1);

    if (same) {
      EXPECT_FALSE(cmp1);
      EXPECT_FALSE(cmp2);
    } else {
      EXPECT_NE(cmp1, cmp2);
    }
  };

  f(nnsp1, nnsp1, /* same */ true);
  f(nnsp1, nnsp2, /* same */ false);
  nnsp1.owner_before(sp); // compiles
}

/**
 * Non-member not_null helpers.
 */
TEST_F(NotNullHelperTest, maker) {
  auto nnu = make_not_null_unique<int>(7);
  EXPECT_EQ(*nnu, 7);

  auto nns = make_not_null_shared<int>(8);
  EXPECT_EQ(*nns, 8);
}

TEST_F(NotNullTest, cmp_correctness) {
  int* a = new int(6);
  int* b = new int(7);

  not_null<int*> nna = a;
  not_null<int*> nnb = b;

#define FB_NOT_NULL_CHECK_OP(op) \
  do {                           \
    bool cmp1 = a op b;          \
    bool cmp2 = nna op nnb;      \
    EXPECT_EQ(cmp1, cmp2);       \
    bool self1 = a op a;         \
    bool self2 = nna op nna;     \
    EXPECT_EQ(self1, self2);     \
  } while (0)

  FB_NOT_NULL_CHECK_OP(==);
  FB_NOT_NULL_CHECK_OP(!=);
  FB_NOT_NULL_CHECK_OP(<);
  FB_NOT_NULL_CHECK_OP(<=);
  FB_NOT_NULL_CHECK_OP(>);
  FB_NOT_NULL_CHECK_OP(>=);

  delete b;
  delete a;
}

TEST_F(NotNullTest, cmp_types) {
  int i = 5;
  int* p = &i;
  int* j = new int(6);
  not_null<int*> nnp(j);

  EXPECT_TRUE(nnp == nnp);
  EXPECT_FALSE(nnp == p);
  EXPECT_FALSE(p == nnp);
  EXPECT_FALSE(nnp == nullptr);
  EXPECT_FALSE(nullptr == nnp);

  delete j;
}

TEST_F(NotNullHelperTest, output) {
  not_null_shared_ptr<int> nn(new int(5));

  std::stringstream ss1, ss2;
  ss1 << nn;
  ss2 << nn.unwrap();
  auto s1 = ss1.str();
  auto s2 = ss2.str();
  EXPECT_EQ(s1, s2);
  EXPECT_NE(s2, "");
}

TEST_F(NotNullHelperTest, casting) {
  not_null_shared_ptr<Derived> nnd(new Derived());

  auto s = static_pointer_cast<Base>(nnd);
  EXPECT_EQ(s.get(), nnd.get());
  static_assert(
      std::is_same_v<decltype(s), not_null_shared_ptr<Base>>, "wrong cast");

  auto d1 = dynamic_pointer_cast<Derived>(s);
  EXPECT_EQ(d1.get(), nnd.get());
  static_assert(
      std::is_same_v<decltype(d1), std::shared_ptr<Derived>>, "wrong cast");
  auto d2 = dynamic_pointer_cast<Derived2>(s);
  EXPECT_EQ(d2.get(), nullptr);
  static_assert(
      std::is_same_v<decltype(d2), std::shared_ptr<Derived2>>, "wrong cast");

  auto c = const_pointer_cast<const Derived>(nnd);
  EXPECT_EQ(c.get(), nnd.get());
  static_assert(
      std::is_same_v<decltype(c), not_null_shared_ptr<const Derived>>,
      "wrong cast");

  auto r = reinterpret_pointer_cast<Base>(nnd);
  EXPECT_EQ(r.get(), nnd.get());
  static_assert(
      std::is_same_v<decltype(r), not_null_shared_ptr<Base>>, "wrong cast");
}

TEST_F(NotNullTest, hash) {
  int* i = new int(5);
  {
    std::unordered_set<not_null<int*>> s;
    s.emplace(i);
  }
  delete i;
}

TEST_F(NotNullTest, null_deleter) {
  int* j = new int(6);
  try {
    not_null_unique_ptr<int, void (*)(int*)> nnui(j, nullptr);
  } catch (std::invalid_argument&) {
    delete j;
    return;
  }
  EXPECT_FALSE(true);
}

template <typename Aliased>
void testAliasedSharedPtrNullOwner() {
  int* p = new int(7);

  {
    std::shared_ptr<int> sp; // null
    Aliased aliased_sp(sp, p);

    // Despite having a null owner, the pointer is valid.
    EXPECT_EQ(*aliased_sp, *p);
    *p = 8;
    EXPECT_EQ(*aliased_sp, *p);
  }

  // ASAN will abort if aliased_sp deleted p.
  EXPECT_EQ(*p, 8);
  delete p;
}

TEST_F(NotNullSharedPtrTest, null_aliased_shared_ptr) {
  // It is legal to construct an aliased shared_ptr with a null owner.
  // Verify that this is so for regular shared_ptr.
  testAliasedSharedPtrNullOwner<std::shared_ptr<int>>();

  // It works for std::shared_ptr, so should also work for not_null_shared_ptr.
  testAliasedSharedPtrNullOwner<not_null_shared_ptr<int>>();
}

TEST_F(NotNullSharedPtrTest, pointer_cast_check) {
  auto nnd = make_not_null_shared<Derived>();

  auto nnb1 = static_pointer_cast<Base>(nnd);
  EXPECT_EQ(nnb1.use_count(), 2);
  EXPECT_EQ(nnd.get(), nnb1.get());

  nullify(nnd);
#ifndef NDEBUG
  EXPECT_DEATH(
      static_pointer_cast<Base>(nnd), "not_null internal pointer is null");
#else
  auto nnb2 = static_pointer_cast<Base>(nnd);
  EXPECT_EQ(nnb2.get(), nullptr);
#endif
}

class MyAllocator : public std::allocator<int> {
 public:
  explicit MyAllocator(int* count) : count_(count) {}

  template <typename... Args>
  int* allocate(Args&&... args) {
    ++*count_;
    Alloc alloc;
    return AllocTraits::allocate(alloc, std::forward<Args>(args)...);
  }

  template <typename U, typename... Args>
  void construct(U* p, Args&&... args) {
    ++*count_;
    Alloc alloc;
    AllocTraits::construct(alloc, p, std::forward<Args>(args)...);
  }

 public:
  using Alloc = std::allocator<int>;
  using AllocTraits = std::allocator_traits<Alloc>;
  using value_type = AllocTraits::value_type;

  using pointer = AllocTraits::pointer;
  using const_pointer = AllocTraits::const_pointer;
  using reference = value_type&;
  using const_reference = value_type const&;
  using size_type = AllocTraits::size_type;
  using difference_type = AllocTraits::difference_type;

  using propagate_on_container_move_assignment =
      AllocTraits::propagate_on_container_move_assignment;
#if __cplusplus <= 202001L
  using Alloc::is_always_equal;
  using Alloc::rebind;
#else
  using is_always_equal = AllocTraits::is_always_equal;
  template <class U>
  struct rebind {
    using other = std::allocator<U>;
  };
#endif

 private:
  int* count_;
};
TEST_F(NotNullSharedPtrTest, allocate_shared) {
  int count = 0;
  auto nnsp = allocate_not_null_shared<int>(MyAllocator(&count), 7);
  EXPECT_EQ(*nnsp, 7);
  EXPECT_EQ(count, 1);
}
