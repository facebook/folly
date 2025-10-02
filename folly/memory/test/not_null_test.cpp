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
#include <stdexcept>
#include <unordered_set>

#include <folly/portability/GTest.h>

using namespace folly;

// Alternative null handler that uses different exception types
struct alternative_null_handler {
  [[noreturn]] static void handle_terminate(const char* msg) {
    folly::terminate_with<std::logic_error>(msg);
  }

  [[noreturn]] static void handle_throw(const char* msg) {
    folly::throw_exception<std::logic_error>(msg);
  }
};

// Test parameter struct to hold handler type and expected exception type
template <typename HandlerT, typename ExceptionT>
struct NullHandlerTestParam final {
  using handler_type = HandlerT;
  using exception_type = ExceptionT;

  template <typename T>
  using not_null = not_null<T, handler_type>;

  template <typename T, typename Deleter = std::default_delete<T>>
  using not_null_unique_ptr = not_null_unique_ptr<T, Deleter, handler_type>;

  template <typename T>
  using not_null_shared_ptr = not_null_shared_ptr<T, handler_type>;

  template <typename To, typename From>
  static bool ctor_throws(From&& from) {
    try {
      not_null<To> nn(std::forward<From>(from));
      EXPECT_TRUE(nn);
    } catch (const exception_type&) {
      return true;
    }
    return false;
  }
};

// Type aliases for test parameters
using DefaultHandlerParam =
    NullHandlerTestParam<default_null_handler, std::invalid_argument>;
using AlternativeHandlerParam =
    NullHandlerTestParam<alternative_null_handler, std::logic_error>;

template <typename T>
class NotNullTest : public testing::Test {};

template <typename T>
class NotNullHelperTest : public testing::Test {};

template <typename T>
class NotNullUniquePtrTest : public testing::Test {};

template <typename T>
class NotNullSharedPtrTest : public testing::Test {};

TYPED_TEST_SUITE_P(NotNullTest);
TYPED_TEST_SUITE_P(NotNullHelperTest);
TYPED_TEST_SUITE_P(NotNullUniquePtrTest);
TYPED_TEST_SUITE_P(NotNullSharedPtrTest);

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

template <typename PtrT, typename NullHandlerT>
void nullify(not_null<PtrT, NullHandlerT>& nn) {
  // Super bad practice, but useful for testing.
  const_cast<PtrT&>(nn.unwrap()) = nullptr;
}

template <typename T>
struct Wrapper {
  template <typename U>
  /* implicit */ Wrapper(U&& u) : t(std::forward<U>(u)) {}

  T t;
};

TYPED_TEST_P(NotNullTest, is_not_null) {
  static_assert(detail::is_not_null_v<void> == false, "is_not_null failure");
  static_assert(detail::is_not_null_v<int> == false, "is_not_null failure");
  static_assert(detail::is_not_null_v<int*> == false, "is_not_null failure");
  static_assert(
      detail::is_not_null_v<const int* const> == false, "is_not_null failure");
  static_assert(
      detail::is_not_null_v<typename TypeParam::template not_null<int*>> ==
          true,
      "is_not_null failure");
}

TYPED_TEST_P(NotNullTest, ctor_exception) {
  using not_null_base = typename TypeParam::template not_null<Base*>;
  using not_null_derived = typename TypeParam::template not_null<Derived*>;

  Derived* dp = nullptr;

  Derived d;
  not_null_derived nnd(&d);
  nullify(nnd);

  EXPECT_TRUE(TypeParam::template ctor_throws<Derived*>(dp));
  EXPECT_TRUE(TypeParam::template ctor_throws<Base*>(dp));
  EXPECT_TRUE(TypeParam::template ctor_throws<Base*>(std::move(dp)));

  // Copy/move constructor never throws
  EXPECT_FALSE(TypeParam::template ctor_throws<Derived*>(nnd));
  EXPECT_FALSE(TypeParam::template ctor_throws<Derived*>(
      const_cast<const not_null_derived&>(nnd)));

  // Converting constructor fails in debug mode
  if constexpr (folly::kIsDebug) {
    EXPECT_DEATH(not_null_base nb1(nnd), "not_null internal pointer is null");
    EXPECT_DEATH(
        not_null_base nb2(std::move(nnd)), "not_null internal pointer is null");
  } else {
    EXPECT_FALSE(TypeParam::template ctor_throws<Base*>(nnd));
    EXPECT_FALSE(TypeParam::template ctor_throws<Base*>(std::move(nnd)));
  }
}

TYPED_TEST_P(NotNullTest, ctor_conversion) {
  int* p = new int(7);
  typename TypeParam::template not_null_unique_ptr<int> a(p);
  typename TypeParam::template not_null_shared_ptr<int> b(std::move(a));
  EXPECT_EQ(*b, 7);
}

TYPED_TEST_P(NotNullTest, explicit_construction) {
  int* i = new int(7);

  // Can explicitly construct a unique_ptr from a raw pointer...
  typename TypeParam::template not_null_unique_ptr<int> a(i);
  EXPECT_EQ(*a, 7);
  // ...but not implicitly; the following code does not (and should not)
  // compile.
#if 0
  int* j = new int(8);
  auto f = [](TypeParam::not_null_unique_ptr<int>) {};
  f(j);
#endif
}

TYPED_TEST_P(NotNullTest, dereferencing) {
  typename TypeParam::template not_null_unique_ptr<std::vector<int>> nn =
      std::make_unique<std::vector<int>>();

  nn->push_back(2);
  EXPECT_EQ((*nn)[0], 2);
}

TYPED_TEST_P(NotNullTest, bool_cast) {
  int i = 7;
  typename TypeParam::template not_null<int*> nn(&i);
  if (nn) {
  } else {
    EXPECT_FALSE(true);
  }
  if (!nn) {
    EXPECT_FALSE(true);
  }
}

TYPED_TEST_P(NotNullTest, ptr_casting) {
  int i = 7;
  typename TypeParam::template not_null<int*> nn(&i);
  auto f = [](int* p) { *p = 8; };
  f(nn);
  EXPECT_EQ(i, 8);
}

TYPED_TEST_P(NotNullTest, conversion_casting) {
  Derived d;
  typename TypeParam::template not_null<Derived*> nnd(&d);
  auto f = [&](Base* b) { EXPECT_EQ(&d, b); };
  f(nnd);
}

TYPED_TEST_P(NotNullTest, move_casting) {
  typename TypeParam::template not_null<std::unique_ptr<Derived>> nnd =
      std::make_unique<Derived>();
  auto f = [](std::unique_ptr<Base>) {};
  f(std::move(nnd));

  if constexpr (folly::kIsDebug) {
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_DEATH(nnd.unwrap(), "not_null internal pointer is null");
  } else {
    // use-after-move is disallowed, but possible
    EXPECT_EQ(nnd.unwrap(), nullptr);
  }
}

// If there are multiple ways to convert, not_null's casting operator should
// yield. If the casting operator did not yield, then Wrapper could either be
// implicitly constructed from not_null, or not_null could be casted to Wrapper,
// leading to a compiler error for ambiguity.
TYPED_TEST_P(NotNullTest, multi_cast) {
  int i = 5;
  typename TypeParam::template not_null<int*> nn(&i);
  auto f = [](Wrapper<typename TypeParam::template not_null<int*>> w) {
    EXPECT_EQ(*w.t, 5);
  };

  const auto& cnn = nn;
  f(cnn);

  f(std::move(nn));
}

TYPED_TEST_P(NotNullTest, unwrap) {
  typename TypeParam::template not_null_unique_ptr<int> nn =
      std::make_unique<int>(7);
  auto f = [](const std::unique_ptr<int>& u_i) { return *u_i; };
  auto g = [](std::unique_ptr<int>&& u_i) { return *u_i; };
  EXPECT_EQ(f(nn.unwrap()), 7);
  EXPECT_EQ(g(std::move(nn).unwrap()), 7);

  // Because g accepts an rvalue-reference, rather than a value, and does not
  // move-from the argument, nn.unwrap() is still valid.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_NE(nn.unwrap(), nullptr);
}

/**
 * not_null_unique_ptr API tests
 */
TYPED_TEST_P(NotNullUniquePtrTest, deleter) {
  int counter = 0;
  {
    typename TypeParam::template not_null_unique_ptr<int, my_deleter> a(
        new int(5), my_deleter(&counter));
    auto d = a.get_deleter();
    EXPECT_EQ(d.counter_, &counter);
  }
  EXPECT_EQ(counter, 1);
}

TYPED_TEST_P(NotNullUniquePtrTest, reset) {
  typename TypeParam::template not_null_unique_ptr<int> a(new int(5));
  int* i = new int(6);

  a.reset(i);
  EXPECT_EQ(*a, 6);
}

TYPED_TEST_P(NotNullUniquePtrTest, swap) {
  using not_null_unique_ptr_int =
      typename TypeParam::template not_null_unique_ptr<int>;
  not_null_unique_ptr_int a(new int(5));
  not_null_unique_ptr_int b(new int(6));

  a.swap(b);
  EXPECT_EQ(*a, 6);
  EXPECT_EQ(*b, 5);
}

TYPED_TEST_P(NotNullUniquePtrTest, get) {
  int* i = new int(5);
  typename TypeParam::template not_null_unique_ptr<int> a(i);
  int* p = a.get();
  EXPECT_EQ(p, i);

  auto g = a.get();
  static_assert(
      detail::is_not_null_v<decltype(g)>, "get() does not return not_null");
}

TYPED_TEST_P(NotNullUniquePtrTest, assignment) {
  typename TypeParam::template not_null_unique_ptr<int> nnup(new int(5));
  auto up = std::make_unique<int>(6);

  nnup = std::move(up);
  EXPECT_EQ(*nnup, 6);
}

TYPED_TEST_P(NotNullUniquePtrTest, conversion_from_default_handler) {
  // Test move construction
  DefaultHandlerParam::not_null_unique_ptr<int> nnup_default_move(new int(42));
  typename TypeParam::template not_null_unique_ptr<int> nnup_move(
      std::move(nnup_default_move));
  EXPECT_EQ(*nnup_move, 42);

  // Test move assignment
  DefaultHandlerParam::not_null_unique_ptr<int> nnup_default_move2(new int(55));
  typename TypeParam::template not_null_unique_ptr<int> nnup_assign(
      new int(24));
  nnup_assign = std::move(nnup_default_move2);
  EXPECT_EQ(*nnup_assign, 55);

  // Test converting move construction (Derived* to Base*)
  DefaultHandlerParam::not_null_unique_ptr<Derived> nnupd_default_move(
      new Derived());
  typename TypeParam::template not_null_unique_ptr<Base> nnupb_move(
      std::move(nnupd_default_move));
  EXPECT_NE(nnupb_move.get().unwrap(), nullptr);

  // Test reset() with not_null from default handler
  DefaultHandlerParam::not_null<int*> nn_default_for_reset(new int(123));
  typename TypeParam::template not_null_unique_ptr<int> nnup_reset(
      new int(100));
  nnup_reset.reset(nn_default_for_reset);
  EXPECT_EQ(*nnup_reset, 123);
}

/**
 * not_null_shared_ptr API tests
 */
TYPED_TEST_P(NotNullSharedPtrTest, deleter) {
  int counter = 0;
  {
    typename TypeParam::template not_null_shared_ptr<int> nnsp(
        new int(5), my_deleter(&counter));

    auto* deleter = get_deleter<my_deleter>(nnsp);
    EXPECT_EQ(deleter->counter_, &counter);
  }
  EXPECT_EQ(counter, 1);

  // Also test that the first argument can be a not_null pointer.
  {
    typename TypeParam::template not_null<int*> nnp(new int(6));
    typename TypeParam::template not_null_shared_ptr<int> nnsp(
        nnp, my_deleter(&counter));
    auto* deleter = get_deleter<my_deleter>(nnsp);
    EXPECT_EQ(deleter->counter_, &counter);
  }
  EXPECT_EQ(counter, 2);
}

TYPED_TEST_P(NotNullSharedPtrTest, aliasing) {
  using not_null_shared_ptr_int =
      typename TypeParam::template not_null_shared_ptr<int>;

  auto sp = std::make_shared<int>(5);
  int i = 6;
  not_null_shared_ptr_int nnsp1(sp, &i);
  int j = 7;
  not_null_shared_ptr_int nnsp2(nnsp1, &j);

  EXPECT_EQ(*sp, 5);
  EXPECT_EQ(*nnsp1, 6);
  EXPECT_EQ(*nnsp2, 7);
  EXPECT_EQ(sp.use_count(), 3);
  EXPECT_EQ(nnsp1.use_count(), 3);
  EXPECT_EQ(nnsp2.use_count(), 3);

  // The move-aliasing-constructor is a C++20 API, and falls back to the const&
  // API without C++20 support. Cannot therefore test use_count().
  const not_null_shared_ptr_int nnsp3(std::move(sp), &i);
  EXPECT_EQ(*nnsp3, 6);
  const not_null_shared_ptr_int nnsp4(std::move(nnsp1), &j);
  EXPECT_EQ(*nnsp4, 7);
}

TYPED_TEST_P(NotNullSharedPtrTest, null_aliasing) {
  int* i = new int(5);
  int* j = new int(6);
  const std::shared_ptr<int> sp1;
  const std::shared_ptr<int> sp2(sp1, i);
  ASSERT_NE(sp2.get(), nullptr);
  EXPECT_EQ(*sp2, 5);
  EXPECT_EQ(sp2.use_count(), 0);

  typename TypeParam::template not_null_shared_ptr<int> nnsp(sp1, j);
  EXPECT_EQ(*nnsp, 6);
  EXPECT_EQ(nnsp.use_count(), 0);

  // Null-aliased pointers do not get deleted.
  delete j;
  delete i;
}

TYPED_TEST_P(NotNullSharedPtrTest, assignment) {
  typename TypeParam::template not_null_shared_ptr<int> nnsp(new int(5));
  auto& ret = nnsp = std::make_unique<int>(6);
  EXPECT_EQ(*nnsp, 6);
  static_assert(
      std::is_same<
          decltype(ret),
          typename TypeParam::template not_null_shared_ptr<int>&>::value,
      "operator= wrong return type");
}

TYPED_TEST_P(NotNullSharedPtrTest, reset) {
  typename TypeParam::template not_null_shared_ptr<int> nnsp(new int(5));
  typename TypeParam::template not_null<int*> nnp1(new int(6));

  nnsp.reset(nnp1);
  EXPECT_EQ(*nnsp, 6);

  int* n = nullptr;
  EXPECT_THROW(nnsp.reset(n), typename TypeParam::exception_type);
  EXPECT_EQ(*nnsp, 6); // Strong exception guarantee.

  int counter = 0;
  nnsp.reset(new int(7), my_deleter(&counter));
  EXPECT_EQ(*nnsp, 7);
  typename TypeParam::template not_null<int*> nnp2(new int(8));
  nnsp.reset(nnp2);
  EXPECT_EQ(counter, 1);
}

TYPED_TEST_P(NotNullSharedPtrTest, swap) {
  using not_null_shared_ptr_int =
      typename TypeParam::template not_null_shared_ptr<int>;
  not_null_shared_ptr_int a(new int(5));
  not_null_shared_ptr_int b(new int(6));

  a.swap(b);
  EXPECT_EQ(*a, 6);
  EXPECT_EQ(*b, 5);
}

TYPED_TEST_P(NotNullSharedPtrTest, get) {
  typename TypeParam::template not_null_shared_ptr<int> nnsp(new int(5));
  auto p = nnsp.get();
  EXPECT_EQ(*p, 5);
  EXPECT_EQ(*nnsp, 5);
  static_assert(
      std::is_same_v<decltype(p), typename TypeParam::template not_null<int*>>,
      "wrong return type");
}

TYPED_TEST_P(NotNullSharedPtrTest, use_count) {
  using not_null_shared_ptr_int =
      typename TypeParam::template not_null_shared_ptr<int>;

  const not_null_shared_ptr_int nnsp1(new int(5));
  EXPECT_EQ(nnsp1.use_count(), 1);
  {
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const not_null_shared_ptr_int nnsp2(nnsp1);
    EXPECT_EQ(nnsp1.use_count(), 2);
    EXPECT_EQ(nnsp2.use_count(), 2);
  }
  EXPECT_EQ(nnsp1.use_count(), 1);
}

TYPED_TEST_P(NotNullSharedPtrTest, owner_before) {
  using not_null_shared_ptr_int =
      typename TypeParam::template not_null_shared_ptr<int>;
  const not_null_shared_ptr_int nnsp1(new int(5));
  const not_null_shared_ptr_int nnsp2(new int(6));
  auto sp = std::make_shared<int>(7);

  auto f = [](const auto& p1, const auto& p2, bool same) {
    const bool cmp1 = p1.owner_before(p2);
    const bool cmp2 = p2.owner_before(p1);

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
TYPED_TEST_P(NotNullHelperTest, maker) {
  const auto nnu = make_not_null_unique<int>(7);
  EXPECT_EQ(*nnu, 7);

  const auto nns = make_not_null_shared<int>(8);
  EXPECT_EQ(*nns, 8);
}

TYPED_TEST_P(NotNullTest, cmp_correctness) {
  int* a = new int(6);
  int* b = new int(7);

  using not_null_int = typename TypeParam::template not_null<int*>;
  not_null_int nna = a;
  not_null_int nnb = b;

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

  // Test binary operators between not_null_int and not_null with default
  // handler
  using not_null_default = DefaultHandlerParam::not_null<int*>;
  not_null_default nna_def = a;
  not_null_default nnb_def = b;

#define FB_NOT_NULL_CHECK_CROSS_OP(op) \
  do {                                 \
    bool cmp1 = a op b;                \
    bool cmp2 = nna op nnb_def;        \
    bool cmp3 = nna_def op nnb;        \
    EXPECT_EQ(cmp1, cmp2);             \
    EXPECT_EQ(cmp1, cmp3);             \
    bool self1 = a op a;               \
    bool self2 = nna op nna_def;       \
    bool self3 = nna_def op nna;       \
    EXPECT_EQ(self1, self2);           \
    EXPECT_EQ(self1, self3);           \
  } while (0)

  FB_NOT_NULL_CHECK_CROSS_OP(==);
  FB_NOT_NULL_CHECK_CROSS_OP(!=);
  FB_NOT_NULL_CHECK_CROSS_OP(<);
  FB_NOT_NULL_CHECK_CROSS_OP(<=);
  FB_NOT_NULL_CHECK_CROSS_OP(>);
  FB_NOT_NULL_CHECK_CROSS_OP(>=);

  delete b;
  delete a;
}

TYPED_TEST_P(NotNullTest, cmp_types) {
  int i = 5;
  int* p = &i;
  int* j = new int(6);
  typename TypeParam::template not_null<int*> nnp(j);

  EXPECT_TRUE(nnp == nnp);
  EXPECT_FALSE(nnp == p);
  EXPECT_FALSE(p == nnp);
  EXPECT_FALSE(nnp == nullptr);
  EXPECT_FALSE(nullptr == nnp);

  delete j;
}

TYPED_TEST_P(NotNullHelperTest, output) {
  typename TypeParam::template not_null_shared_ptr<int> nn(new int(5));

  std::stringstream ss1, ss2;
  ss1 << nn;
  ss2 << nn.unwrap();
  auto s1 = ss1.str();
  auto s2 = ss2.str();
  EXPECT_EQ(s1, s2);
  EXPECT_NE(s2, "");
}

TYPED_TEST_P(NotNullHelperTest, casting) {
  auto nnu = make_not_null_unique<int>(42);
  std::unique_ptr<int> i{std::move(nnu)};
  static_assert(
      std::is_constructible_v<
          std::unique_ptr<int>,
          typename TypeParam::template not_null<std::unique_ptr<int>>>);

  typename TypeParam::template not_null_shared_ptr<Derived> nnd(new Derived());

  auto s =
      static_pointer_cast<Base, Derived, typename TypeParam::handler_type>(nnd);
  EXPECT_EQ(s.get(), nnd.get());
  static_assert(
      std::is_same_v<
          decltype(s),
          typename TypeParam::template not_null_shared_ptr<Base>>,
      "wrong cast");

  auto d1 =
      dynamic_pointer_cast<Derived, Base, typename TypeParam::handler_type>(s);
  EXPECT_EQ(d1.get(), nnd.get());
  static_assert(
      std::is_same_v<decltype(d1), std::shared_ptr<Derived>>, "wrong cast");
  auto d2 =
      dynamic_pointer_cast<Derived2, Base, typename TypeParam::handler_type>(s);
  EXPECT_EQ(d2.get(), nullptr);
  static_assert(
      std::is_same_v<decltype(d2), std::shared_ptr<Derived2>>, "wrong cast");

  auto c = const_pointer_cast<
      const Derived,
      Derived,
      typename TypeParam::handler_type>(nnd);
  EXPECT_EQ(c.get(), nnd.get());
  static_assert(
      std::is_same_v<
          decltype(c),
          typename TypeParam::template not_null_shared_ptr<const Derived>>,
      "wrong cast");

  auto r =
      reinterpret_pointer_cast<Base, Derived, typename TypeParam::handler_type>(
          nnd);
  EXPECT_EQ(r.get(), nnd.get());
  static_assert(
      std::is_same_v<
          decltype(r),
          typename TypeParam::template not_null_shared_ptr<Base>>,
      "wrong cast");
}

TYPED_TEST_P(NotNullTest, hash) {
  int* i = new int(5);
  {
    std::unordered_set<typename TypeParam::template not_null<int*>> s;
    s.emplace(i);
  }
  delete i;
}

TYPED_TEST_P(NotNullTest, conversion_from_default_handler) {
  int i = 42;
  Derived d;

  // Create not_null with default null handler
  DefaultHandlerParam::not_null<int*> nn_default(&i);
  DefaultHandlerParam::not_null<Derived*> nnd_default(&d);

  // Test copy construction
  typename TypeParam::template not_null<int*> nn_copy(nn_default);
  EXPECT_EQ(*nn_copy, 42);

  // Test move construction
  DefaultHandlerParam::not_null<int*> nn_default_move(&i);
  typename TypeParam::template not_null<int*> nn_move(
      std::move(nn_default_move));
  EXPECT_EQ(*nn_move, 42);

  // Test copy assignment
  int j = 24;
  typename TypeParam::template not_null<int*> nn_assign(&j);
  nn_assign = nn_default;
  EXPECT_EQ(*nn_assign, 42);

  // Test move assignment
  DefaultHandlerParam::not_null<int*> nn_default_move2(&i);
  nn_assign = std::move(nn_default_move2);
  EXPECT_EQ(*nn_assign, 42);

  // Test converting copy construction (Derived* to Base*)
  typename TypeParam::template not_null<Base*> nnb_copy(nnd_default);
  EXPECT_EQ(nnb_copy.unwrap(), &d);

  // Test converting move construction (Derived* to Base*)
  DefaultHandlerParam::not_null<Derived*> nnd_default_move(&d);
  typename TypeParam::template not_null<Base*> nnb_move(
      std::move(nnd_default_move));
  EXPECT_EQ(nnb_move.unwrap(), &d);
}

TYPED_TEST_P(NotNullTest, null_deleter) {
  int* j = new int(6);
  try {
    typename TypeParam::template not_null_unique_ptr<int, void (*)(int*)> nnui(
        j, nullptr);
  } catch (const typename TypeParam::exception_type&) {
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

TYPED_TEST_P(NotNullSharedPtrTest, null_aliased_shared_ptr) {
  // It is legal to construct an aliased shared_ptr with a null owner.
  // Verify that this is so for regular shared_ptr.
  testAliasedSharedPtrNullOwner<std::shared_ptr<int>>();

  // It works for std::shared_ptr, so should also work for not_null_shared_ptr.
  testAliasedSharedPtrNullOwner<
      typename TypeParam::template not_null_shared_ptr<int>>();
}

TYPED_TEST_P(NotNullSharedPtrTest, pointer_cast_check) {
  auto nnd = make_not_null_shared<Derived>();

  auto nnb1 =
      static_pointer_cast<Base, Derived, typename TypeParam::handler_type>(nnd);
  EXPECT_EQ(nnb1.use_count(), 2);
  EXPECT_EQ(nnd.get(), nnb1.get());

  nullify(nnd);
  if constexpr (folly::kIsDebug) {
    EXPECT_DEATH(
        (static_pointer_cast<Base, Derived, typename TypeParam::handler_type>(
            nnd)),
        "not_null internal pointer is null");
  } else {
    auto nnb2 =
        static_pointer_cast<Base, Derived, typename TypeParam::handler_type>(
            nnd);
    EXPECT_EQ(nnb2.get(), nullptr);
  }
}

template <typename T>
class MyAllocator : public std::allocator<T> {
 public:
  explicit MyAllocator(int* c) : count_(c) {}
  template <typename U>
  explicit MyAllocator(const MyAllocator<U>& other) {
    count_ = other.count_;
  }

  template <typename... Args>
  T* allocate(Args&&... args) {
    ++*count_;
    return std::allocator<T>::allocate(std::forward<Args>(args)...);
  }

  template <typename U, typename... Args>
  void construct(U* p, Args&&... args) {
    ++*count_;
    ::new ((void*)p) U(std::forward<Args>(args)...);
  }

  int* count_;
};

TYPED_TEST_P(NotNullSharedPtrTest, allocate_shared) {
  int count = 0;
  auto nnsp = allocate_not_null_shared<int>(MyAllocator<int>(&count), 7);
  EXPECT_EQ(*nnsp, 7);
  EXPECT_EQ(count, 2);
}

TYPED_TEST_P(NotNullSharedPtrTest, conversion_from_default_handler) {
  // Create not_null_shared_ptr with default null handler
  DefaultHandlerParam::not_null_shared_ptr<int> nnsp_default(new int(42));
  DefaultHandlerParam::not_null_shared_ptr<Derived> nnspd_default(
      new Derived());

  // Test copy construction
  typename TypeParam::template not_null_shared_ptr<int> nnsp_copy(nnsp_default);
  EXPECT_EQ(*nnsp_copy, 42);
  EXPECT_EQ(nnsp_copy.use_count(), 2);

  // Test move construction
  DefaultHandlerParam::not_null_shared_ptr<int> nnsp_default_move(new int(42));
  typename TypeParam::template not_null_shared_ptr<int> nnsp_move(
      std::move(nnsp_default_move));
  EXPECT_EQ(*nnsp_move, 42);

  // Test copy assignment
  typename TypeParam::template not_null_shared_ptr<int> nnsp_assign(
      new int(24));
  nnsp_assign = nnsp_default;
  EXPECT_EQ(*nnsp_assign, 42);
  EXPECT_EQ(nnsp_assign.use_count(), 3); // original + copy + assign

  // Test move assignment
  DefaultHandlerParam::not_null_shared_ptr<int> nnsp_default_move2(new int(42));
  nnsp_assign = std::move(nnsp_default_move2);
  EXPECT_EQ(*nnsp_assign, 42);

  // Test converting copy construction (Derived* to Base*)
  typename TypeParam::template not_null_shared_ptr<Base> nnspb_copy(
      nnspd_default);
  EXPECT_EQ(nnspb_copy.get().unwrap(), nnspd_default.get().unwrap());
  EXPECT_EQ(nnspb_copy.use_count(), 2);

  // Test converting move construction (Derived* to Base*)
  DefaultHandlerParam::not_null_shared_ptr<Derived> nnspd_default_move(
      new Derived());
  typename TypeParam::template not_null_shared_ptr<Base> nnspb_move(
      std::move(nnspd_default_move));
  EXPECT_NE(nnspb_move.get().unwrap(), nullptr);

  // Test aliasing construction from default handler shared_ptr
  DefaultHandlerParam::not_null_shared_ptr<int> nnsp_for_alias(new int(999));
  int aliased_value = 123;
  typename TypeParam::template not_null_shared_ptr<int> nnsp_aliased(
      nnsp_for_alias, &aliased_value);
  EXPECT_EQ(*nnsp_aliased, 123);
  EXPECT_EQ(nnsp_aliased.use_count(), 2);

  // Test aliasing construction from default handler shared_ptr (move version)
  DefaultHandlerParam::not_null_shared_ptr<int> nnsp_for_alias_move(
      new int(999));
  typename TypeParam::template not_null_shared_ptr<int> nnsp_aliased_move(
      std::move(nnsp_for_alias_move), &aliased_value);
  EXPECT_EQ(*nnsp_aliased_move, 123);

  // Test reset() with not_null from default handler
  DefaultHandlerParam::not_null<int*> nn_default_for_reset(new int(555));
  typename TypeParam::template not_null_shared_ptr<int> nnsp_reset(
      new int(100));
  nnsp_reset.reset(nn_default_for_reset);
  EXPECT_EQ(*nnsp_reset, 555);
}

// Register parameterized tests for NotNullTest
REGISTER_TYPED_TEST_SUITE_P(
    NotNullTest,
    is_not_null,
    ctor_exception,
    ctor_conversion,
    explicit_construction,
    dereferencing,
    bool_cast,
    ptr_casting,
    conversion_casting,
    move_casting,
    multi_cast,
    unwrap,
    cmp_correctness,
    cmp_types,
    hash,
    conversion_from_default_handler,
    null_deleter);

// Register parameterized tests for NotNullUniquePtrTest
REGISTER_TYPED_TEST_SUITE_P(
    NotNullUniquePtrTest,
    deleter,
    reset,
    swap,
    get,
    assignment,
    conversion_from_default_handler);

// Register parameterized tests for NotNullSharedPtrTest
REGISTER_TYPED_TEST_SUITE_P(
    NotNullSharedPtrTest,
    deleter,
    aliasing,
    null_aliasing,
    assignment,
    reset,
    swap,
    get,
    use_count,
    owner_before,
    null_aliased_shared_ptr,
    pointer_cast_check,
    allocate_shared,
    conversion_from_default_handler);

// Register parameterized tests for NotNullHelperTest
REGISTER_TYPED_TEST_SUITE_P(NotNullHelperTest, maker, output, casting);

// Instantiate tests with both handler types
using NullHandlerTypes =
    testing::Types<DefaultHandlerParam, AlternativeHandlerParam>;
INSTANTIATE_TYPED_TEST_SUITE_P(NotNull, NotNullTest, NullHandlerTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(
    NotNullUniquePtr, NotNullUniquePtrTest, NullHandlerTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(
    NotNullSharedPtr, NotNullSharedPtrTest, NullHandlerTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(
    NotNullHelper, NotNullHelperTest, NullHandlerTypes);
