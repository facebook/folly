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

#include <folly/Indirect.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <folly/portability/GTest.h>

using folly::indirect;

// A moved-from indirect is valueless-after-move, a well-defined state that this
// file exercises throughout, so reads of moved-from objects here are
// deliberate.

// NOLINTBEGIN(bugprone-use-after-move)

// Stateful allocator for propagation testing.
template <
    typename T,
    bool PropCopy = false,
    bool PropMove = false,
    bool PropSwap = false>
struct StatefulAlloc {
  using value_type = T;
  using propagate_on_container_copy_assignment =
      std::integral_constant<bool, PropCopy>;
  using propagate_on_container_move_assignment =
      std::integral_constant<bool, PropMove>;
  using propagate_on_container_swap = std::integral_constant<bool, PropSwap>;
  using is_always_equal = std::false_type;

  int id{0};
  explicit StatefulAlloc(int i = 0) : id(i) {}
  template <typename U>
  /* implicit */ StatefulAlloc(
      const StatefulAlloc<U, PropCopy, PropMove, PropSwap>& o)
      : id(o.id) {}
  static std::unordered_map<void*, int>& registry() {
    static std::unordered_map<void*, int> instance;
    return instance;
  }

  T* allocate(std::size_t n) {
    T* p = std::allocator<T>{}.allocate(n);
    registry()[static_cast<void*>(p)] = id;
    return p;
  }
  void deallocate(T* p, std::size_t n) {
    auto& reg = registry();
    auto it = reg.find(static_cast<void*>(p));
    EXPECT_TRUE(it != reg.end())
        << "deallocate of unknown pointer " << static_cast<void*>(p);
    if (it != reg.end()) {
      EXPECT_EQ(it->second, id)
          << "cross-allocator deallocation: allocated with id " << it->second
          << " deallocated with id " << id;
      reg.erase(it);
    }
    std::allocator<T>{}.deallocate(p, n);
  }
  template <typename U, typename... Args>
  void construct(U* p, Args&&... args) {
    ::new (p) U(std::forward<Args>(args)...);
  }
  template <typename U>
  void destroy(U* p) {
    p->~U();
  }
  bool operator==(const StatefulAlloc& o) const { return id == o.id; }
  bool operator!=(const StatefulAlloc& o) const { return id != o.id; }
};

// Type that is copy-constructible but not copy-assignable (const member).
struct NonAssignable {
  const int value;
  explicit NonAssignable(int v) : value(v) {}
  NonAssignable(const NonAssignable&) = default;
  NonAssignable& operator=(const NonAssignable&) = delete;
  bool operator==(const NonAssignable& o) const { return value == o.value; }
};

struct ThrowOnCopy {
  int v{0};
  explicit ThrowOnCopy(int x) : v(x) {}
  ThrowOnCopy(const ThrowOnCopy& o) {
    if (o.v == 42) {
      throw std::runtime_error("copy throw");
    }
    v = o.v;
  }
  ThrowOnCopy& operator=(const ThrowOnCopy&) = default;
  bool operator==(const ThrowOnCopy& o) const { return v == o.v; }
};

// Ordered by operator< alone, with no operator<=>, so indirect's operator<=>
// has to synthesize a weak_ordering rather than delegate to a three-way
// comparison on T.
struct LessOnly {
  int v{0};
  friend bool operator<(const LessOnly& a, const LessOnly& b) {
    return a.v < b.v;
  }
  friend bool operator==(const LessOnly& a, const LessOnly& b) {
    return a.v == b.v;
  }
};

struct AllocCounters {
  int constructed{0};
  int destroyed{0};
};

// Pointer-like type that is not a raw pointer, so that indirect's `pointer` /
// `const_pointer` typedefs and its std::to_address path are exercised.
template <typename T>
struct FancyPtr {
  T* raw{nullptr};

  FancyPtr() = default;
  /* implicit */ FancyPtr(std::nullptr_t) {}
  explicit FancyPtr(T* p) : raw(p) {}
  template <typename U>
    requires std::is_convertible_v<U*, T*>
  /* implicit */ FancyPtr(const FancyPtr<U>& o) : raw(o.raw) {}

  T& operator*() const { return *raw; }
  T* operator->() const { return raw; }
  static FancyPtr pointer_to(T& r) { return FancyPtr(std::addressof(r)); }

  friend bool operator==(FancyPtr a, FancyPtr b) { return a.raw == b.raw; }
  friend bool operator==(FancyPtr a, std::nullptr_t) {
    return a.raw == nullptr;
  }
};

// Allocator with a fancy pointer that also counts allocator_traits::construct
// and ::destroy calls, so tests can prove the owned object is created and
// destroyed through the allocator rather than by placement new.
template <typename T>
struct FancyAlloc {
  using value_type = T;
  using pointer = FancyPtr<T>;
  using is_always_equal = std::false_type;

  AllocCounters* counters{nullptr};

  explicit FancyAlloc(AllocCounters* c) : counters(c) {}
  template <typename U>
  /* implicit */ FancyAlloc(const FancyAlloc<U>& o) : counters(o.counters) {}

  pointer allocate(std::size_t n) {
    return pointer(std::allocator<T>{}.allocate(n));
  }
  void deallocate(pointer p, std::size_t n) {
    std::allocator<T>{}.deallocate(p.raw, n);
  }
  template <typename U, typename... Args>
  void construct(U* p, Args&&... args) {
    ++counters->constructed;
    ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
  }
  template <typename U>
  void destroy(U* p) {
    ++counters->destroyed;
    p->~U();
  }
  bool operator==(const FancyAlloc& o) const { return counters == o.counters; }
};

// Helper to create a valueless indirect (moved-from).
template <typename T, typename... Args>
indirect<T> makeValueless(Args&&... args) {
  indirect<T> x(std::in_place, std::forward<Args>(args)...);
  indirect<T> sink = std::move(x);
  (void)sink;
  return x;
}
template <typename T, typename Alloc, typename... Args>
indirect<T, Alloc> makeValuelessAlloc(const Alloc& alloc, Args&&... args) {
  indirect<T, Alloc> x(
      std::allocator_arg, alloc, std::in_place, std::forward<Args>(args)...);
  indirect<T, Alloc> sink(std::allocator_arg, alloc, std::move(x));
  (void)sink;
  return x;
}

TEST(Indirect, DefaultConstruct) {
  indirect<int> a;
  EXPECT_EQ(*a, 0);
  EXPECT_NE(a.operator->(), nullptr);
  EXPECT_FALSE(a.valueless_after_move());
}

TEST(Indirect, InPlace) {
  indirect<int> b(std::in_place, 42);
  EXPECT_EQ(*b, 42);
  indirect<std::string> s(std::in_place, "hello");
  EXPECT_EQ(*s, "hello");
}

TEST(Indirect, CopyDeep) {
  indirect<int> a(std::in_place, 42);
  indirect<int> b = a;
  EXPECT_EQ(*b, 42);
  EXPECT_NE(&*a, &*b);
  *b = 10;
  EXPECT_EQ(*a, 42);
  EXPECT_EQ(*b, 10);
}

TEST(Indirect, CopyWithAllocator) {
  std::allocator<int> alloc;
  indirect<int> a(std::in_place, 7);
  indirect<int> b(std::allocator_arg, alloc, a);
  EXPECT_EQ(*b, 7);
  EXPECT_EQ(b.get_allocator(), alloc);
}

TEST(Indirect, Move) {
  indirect<int> a(std::in_place, 99);
  indirect<int> b = std::move(a);
  EXPECT_EQ(*b, 99);
  EXPECT_TRUE(a.valueless_after_move());
  a = indirect<int>(std::in_place, 1);
  EXPECT_FALSE(a.valueless_after_move());
  EXPECT_EQ(*a, 1);
  // round-tripping a value through move construction and move assignment
  indirect<int> c = std::move(a);
  a = std::move(c);
  EXPECT_FALSE(a.valueless_after_move());
  // default-constructed is not valueless; moving from it makes source valueless
  indirect<int> empty;
  EXPECT_FALSE(empty.valueless_after_move());
  indirect<int> fromEmpty = std::move(empty);
  EXPECT_TRUE(empty.valueless_after_move());
  EXPECT_FALSE(fromEmpty.valueless_after_move());
  EXPECT_EQ(*fromEmpty, 0);
  // copy and move from valueless yields valueless
  indirect<int> valuelessSrc(std::in_place, 1);
  indirect<int> valuelessHolder = std::move(valuelessSrc);
  EXPECT_TRUE(valuelessSrc.valueless_after_move());
  indirect<int> fromValueless = std::move(valuelessSrc);
  EXPECT_TRUE(fromValueless.valueless_after_move());
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
  indirect<int> copyEmpty(fromValueless);
  EXPECT_TRUE(copyEmpty.valueless_after_move());
}

TEST(Indirect, MoveWithAllocator) {
  std::allocator<int> alloc;
  indirect<int> a(std::allocator_arg, alloc, std::in_place, 123);
  indirect<int> b(std::allocator_arg, alloc, std::move(a));
  EXPECT_EQ(*b, 123);
  EXPECT_TRUE(a.valueless_after_move());
}

TEST(Indirect, MoveWithUnequalAllocator) {
  StatefulAlloc<int, false, false, false> alloc1(1), alloc2(2);
  indirect<int, StatefulAlloc<int, false, false, false>> a(
      std::allocator_arg, alloc1, std::in_place, 10);
  indirect<int, StatefulAlloc<int, false, false, false>> b(
      std::allocator_arg, alloc2, std::move(a));
  EXPECT_EQ(*b, 10);
  EXPECT_TRUE(a.valueless_after_move());
  EXPECT_EQ(b.get_allocator().id, 2);
}

TEST(Indirect, CopyAssignment) {
  indirect<int> a(std::in_place, 1);
  indirect<int> b(std::in_place, 2);
  a = b;
  EXPECT_EQ(*a, 2);
  EXPECT_EQ(*b, 2);
  EXPECT_NE(&*a, &*b);
  auto& aref = a;
  a = aref; // self via ref
  EXPECT_EQ(*a, 2);
}

TEST(Indirect, CopyAssignmentValueless) {
  indirect<int> a(std::in_place, 1);
  indirect<int> b(std::in_place, 2);
  // make tmp valueless via move
  indirect<int> tmp(std::in_place, 99);
  indirect<int> moved = std::move(tmp);
  (void)moved;
  EXPECT_TRUE(tmp.valueless_after_move());
  a = tmp;
  EXPECT_TRUE(a.valueless_after_move());
  tmp = b;
  EXPECT_FALSE(tmp.valueless_after_move());
  EXPECT_EQ(*tmp, 2);
}

TEST(Indirect, CopyAssignMandatesCopyAssignableT) {
  // [indirect.assign] mandates is_copy_assignable_v<T> as well as
  // is_copy_constructible_v<T>. Mandates are not constraints, so the operator
  // stays declared and copy-assigning indirect<NonAssignable> is a static
  // assertion failure at instantiation rather than an unviable overload.
  static_assert(!std::is_copy_assignable_v<NonAssignable>);
  static_assert(std::is_copy_assignable_v<indirect<NonAssignable>>);
}

TEST(Indirect, MoveAssignment) {
  indirect<int> a(std::in_place, 1);
  indirect<int> b(std::in_place, 2);
  a = std::move(b);
  EXPECT_EQ(*a, 2);
  EXPECT_TRUE(b.valueless_after_move());
}

TEST(Indirect, MoveAssignmentUnequalAllocator) {
  StatefulAlloc<int, false, false, false> alloc1(1), alloc2(2);
  indirect<int, StatefulAlloc<int, false, false, false>> a(
      std::allocator_arg, alloc1, std::in_place, 1);
  indirect<int, StatefulAlloc<int, false, false, false>> b(
      std::allocator_arg, alloc2, std::in_place, 2);
  a = std::move(b);
  EXPECT_EQ(*a, 2);
  EXPECT_TRUE(b.valueless_after_move());
  EXPECT_EQ(a.get_allocator().id, 1);
}

TEST(Indirect, PropagatingAllocatorCopyAssign) {
  StatefulAlloc<int, true, false, false> alloc1(1), alloc2(2);
  indirect<int, StatefulAlloc<int, true, false, false>> a(
      std::allocator_arg, alloc1, std::in_place, 1);
  indirect<int, StatefulAlloc<int, true, false, false>> b(
      std::allocator_arg, alloc2, std::in_place, 2);
  a = b;
  EXPECT_EQ(*a, 2);
  EXPECT_EQ(a.get_allocator().id, 2);
}

TEST(Indirect, PropagatingAllocatorMoveAssign) {
  StatefulAlloc<int, false, true, false> alloc1(1), alloc2(2);
  indirect<int, StatefulAlloc<int, false, true, false>> a(
      std::allocator_arg, alloc1, std::in_place, 1);
  indirect<int, StatefulAlloc<int, false, true, false>> b(
      std::allocator_arg, alloc2, std::in_place, 2);
  a = std::move(b);
  EXPECT_EQ(*a, 2);
  EXPECT_EQ(a.get_allocator().id, 2);
  EXPECT_TRUE(b.valueless_after_move());
}

TEST(Indirect, AssignFromValue) {
  indirect<int> a(std::in_place, 1);
  a = 42;
  EXPECT_EQ(*a, 42);
}

TEST(Indirect, AssignFromValueString) {
  indirect<std::string> s(std::in_place, "hello");
  s = std::string("world");
  EXPECT_EQ(*s, "world");
  s = std::string("again");
  EXPECT_EQ(*s, "again");
}

TEST(Indirect, Swap) {
  indirect<int> a(std::in_place, 7), b(std::in_place, 8);
  swap(a, b);
  EXPECT_EQ(*a, 8);
  EXPECT_EQ(*b, 7);
  a.swap(b);
  EXPECT_EQ(*a, 7);
  EXPECT_EQ(*b, 8);
}

TEST(Indirect, SwapPropagating) {
  StatefulAlloc<int, false, false, true> alloc1(1), alloc2(2);
  indirect<int, StatefulAlloc<int, false, false, true>> a(
      std::allocator_arg, alloc1, std::in_place, 1);
  indirect<int, StatefulAlloc<int, false, false, true>> b(
      std::allocator_arg, alloc2, std::in_place, 2);
  a.swap(b);
  EXPECT_EQ(*a, 2);
  EXPECT_EQ(*b, 1);
  EXPECT_EQ(a.get_allocator().id, 2);
  EXPECT_EQ(b.get_allocator().id, 1);
}

TEST(Indirect, Comparison) {
  indirect<int> a(std::in_place, 5), b(std::in_place, 10), c(std::in_place, 5);
  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(a <= b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(a == 5);
  EXPECT_TRUE(5 == a);
  EXPECT_TRUE(a < 10);
  EXPECT_TRUE(10 > a);
  EXPECT_TRUE((a <=> b) < 0);
  EXPECT_TRUE((a <=> c) == 0);
  // valueless comparisons - make tmp valueless via move
  indirect<int> tmp(std::in_place, 1);
  indirect<int> tmp2 = std::move(tmp);
  (void)tmp2;
  // tmp now valueless
  indirect<int> empty = std::move(tmp); // from already-valueless
  EXPECT_TRUE(empty.valueless_after_move());
  EXPECT_TRUE(empty == tmp); // both valueless => equal
  EXPECT_FALSE(empty == a);
  EXPECT_FALSE(a == empty);
  EXPECT_TRUE((empty <=> a) < 0);
  EXPECT_TRUE((a <=> empty) > 0);
}

TEST(Indirect, Hash) {
  indirect<int> a(std::in_place, 5);
  EXPECT_EQ(std::hash<indirect<int>>{}(a), std::hash<int>{}(5));
  indirect<int> tmp(std::in_place, 1);
  indirect<int> moved = std::move(tmp);
  (void)moved;
  EXPECT_TRUE(tmp.valueless_after_move());
  // hash of valueless should not throw and be consistent
  EXPECT_EQ(std::hash<indirect<int>>{}(tmp), 0u);
}

TEST(Indirect, Allocator) {
  std::allocator<int> alloc;
  indirect<int> a(std::allocator_arg, alloc, std::in_place, 99);
  EXPECT_EQ(a.get_allocator(), alloc);
  EXPECT_EQ(*a, 99);
}

TEST(Indirect, CopyDeepUserType) {
  struct Node {
    int value;
    bool operator==(const Node& o) const { return value == o.value; }
  };
  indirect<Node> n(std::in_place, Node{42});
  indirect<Node> m = n;
  EXPECT_EQ(n->value, 42);
  EXPECT_TRUE(n == m);
  m->value = 10;
  EXPECT_EQ(n->value, 42);
  EXPECT_EQ(m->value, 10);
  EXPECT_FALSE(n == m);
}

TEST(Indirect, CompositeWithIndirectMember) {
  struct Inner {
    int x;
    bool operator==(const Inner& o) const { return x == o.x; }
  };
  struct Outer {
    indirect<Inner> inner;
    int y;
    explicit Outer(int v) : inner(std::in_place, Inner{v}), y(v * 2) {}
  };
  Outer a(5);
  Outer b = a;
  EXPECT_EQ(a.inner->x, 5);
  EXPECT_EQ(b.inner->x, 5);
  EXPECT_TRUE(a.inner == b.inner);
  b.inner->x = 10;
  EXPECT_EQ(a.inner->x, 5);
  EXPECT_FALSE(a.inner == b.inner);
  std::vector<indirect<Inner>> vec;
  vec.emplace_back(std::in_place, Inner{1});
  vec.emplace_back(std::in_place, Inner{2});
  auto vec2 = vec;
  EXPECT_EQ(vec2[0]->x, 1);
  vec2[0]->x = 99;
  EXPECT_EQ(vec[0]->x, 1);
}

TEST(Indirect, Dereference) {
  indirect<std::string> s(std::in_place, "hello");
  EXPECT_EQ(s->size(), 5);
  EXPECT_EQ((*s).size(), 5);
  const indirect<std::string> cs(std::in_place, "world");
  EXPECT_EQ(cs->size(), 5);
  EXPECT_EQ((*cs).size(), 5);
}

TEST(Indirect, SizeOptimization) {
#if defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address)
  // Tight size only when [[no_unique_address]] actually collapses the empty
  // allocator. On MSVC ABI or toolchains where the attribute is a no-op, size
  // is 2*sizeof(void*).
  if constexpr (std::is_empty_v<std::allocator<int>>) {
    // When no_unique_address is effective, indirect<int> should be
    // pointer-sized. Otherwise it will be padded to 2 pointers; allow either
    // but ensure no bloat beyond that.
    EXPECT_TRUE(
        sizeof(indirect<int>) == sizeof(void*) ||
        sizeof(indirect<int>) == 2 * sizeof(void*));
    if (sizeof(indirect<int>) == sizeof(void*)) {
      SUCCEED() << "no_unique_address optimized";
    }
  }
#else
  EXPECT_LE(sizeof(indirect<int>), 2 * sizeof(void*));
#endif
}

TEST(Indirect, Constexpr) {
#if defined(__cpp_lib_constexpr_dynamic_alloc) && \
    __cpp_lib_constexpr_dynamic_alloc >= 201907L
  constexpr auto val = [] {
    indirect<int> x(std::in_place, 5);
    return *x;
  }();
  static_assert(val == 5, "constexpr indirect works");
  constexpr auto cval = [] {
    indirect<int> a(std::in_place, 3);
    indirect<int> b(std::in_place, 7);
    a = b;
    return *a;
  }();
  static_assert(cval == 7, "constexpr copy works");
#endif
  indirect<int> c(std::in_place, 3);
  EXPECT_EQ(*c, 3);
  auto val2 = [] {
    indirect<int> x(std::in_place, 5);
    return *x;
  }();
  EXPECT_EQ(val2, 5);
}

TEST(Indirect, InitListNonDefault) {
  struct InitOnly {
    int x;
    InitOnly() = delete;
    /* implicit */ InitOnly(std::initializer_list<int> il) : x(*il.begin()) {}
    bool operator==(const InitOnly& o) const { return x == o.x; }
  };
  indirect<InitOnly> a(std::in_place, {42});
  EXPECT_EQ(a->x, 42);
  indirect<InitOnly> b(
      std::allocator_arg, std::allocator<InitOnly>{}, std::in_place, {7});
  EXPECT_EQ(b->x, 7);
  EXPECT_FALSE(a == b);
}

TEST(Indirect, ValueAssignPreservesAddress) {
  indirect<std::string> s(std::in_place, "hello");
  auto* old = &*s;
  auto* old_ptr = s.operator->();
  s = std::string("world");
  EXPECT_EQ(old, &*s);
  EXPECT_EQ(old_ptr, s.operator->());
  EXPECT_EQ(*s, "world");
  // valueless case reallocates
  indirect<std::string> v;
  // make valueless
  indirect<std::string> tmp(std::in_place, "tmp");
  v = std::move(tmp); // v now holds "tmp", tmp valueless
  indirect<std::string> valueless = std::move(tmp);
  EXPECT_TRUE(tmp.valueless_after_move());
  valueless = std::string("new");
  EXPECT_FALSE(valueless.valueless_after_move());
  EXPECT_EQ(*valueless, "new");
}

TEST(Indirect, PartialOrdering) {
  indirect<double> a(std::in_place, 1.0);
  indirect<double> b(std::in_place, 2.0);
  EXPECT_TRUE((a <=> b) == std::partial_ordering::less);
  EXPECT_TRUE((b <=> a) == std::partial_ordering::greater);
  indirect<double> nan1(
      std::in_place, std::numeric_limits<double>::quiet_NaN());
  auto cmp = (nan1 <=> a);
  EXPECT_TRUE(cmp == std::partial_ordering::unordered);
}

TEST(Indirect, CopyTraitsForMoveOnly) {
  // [indirect.ctor] / [indirect.assign] state copy-constructibility of T as a
  // Mandates, so the copy operations stay declared for a move-only T and the
  // traits report true; copying one is a static assertion failure. The move
  // constructor is unconditionally noexcept, so move_if_noexcept still moves
  // and containers never select the copy path (see MoveOnlyValueTypeInVector).
  static_assert(std::is_copy_constructible_v<indirect<std::unique_ptr<int>>>);
  static_assert(std::is_copy_assignable_v<indirect<std::unique_ptr<int>>>);
  static_assert(std::is_move_constructible_v<indirect<std::unique_ptr<int>>>);
  static_assert(std::is_move_assignable_v<indirect<std::unique_ptr<int>>>);
  indirect<std::unique_ptr<int>> a(std::in_place, std::make_unique<int>(42));
  EXPECT_EQ(**a, 42);
  indirect<std::unique_ptr<int>> b = std::move(a);
  EXPECT_TRUE(a.valueless_after_move());
  EXPECT_EQ(**b, 42);
  // copy from valueless should be allowed and produce valueless
  indirect<std::unique_ptr<int>> c =
      std::move(a); // a is valueless, c should be valueless
  EXPECT_TRUE(c.valueless_after_move());
}

TEST(Indirect, MoveOnlyValueTypeInVector) {
  // indirect's copy operations are declared even for a move-only T, so this
  // pins down that reallocation still picks the move path rather than
  // instantiating the copy constructor and failing its static assertion
  std::vector<indirect<std::unique_ptr<int>>> v;
  v.emplace_back(std::in_place, std::make_unique<int>(1));
  v.emplace_back(std::in_place, std::make_unique<int>(2));
  v.reserve(64);
  v.emplace_back(std::in_place, std::make_unique<int>(3));
  ASSERT_EQ(v.size(), 3);
  EXPECT_EQ(**v[0], 1);
  EXPECT_EQ(**v[1], 2);
  EXPECT_EQ(**v[2], 3);
}

TEST(Indirect, WeakOrderingCategory) {
  struct Weak {
    int v;
    std::weak_ordering operator<=>(const Weak& o) const {
      if (v == o.v) {
        return std::weak_ordering::equivalent;
      }
      return v < o.v ? std::weak_ordering::less : std::weak_ordering::greater;
    }
    bool operator==(const Weak& o) const { return v == o.v; }
  };
  indirect<Weak> a(std::in_place, Weak{1}), b(std::in_place, Weak{2}),
      c(std::in_place, Weak{1});
  EXPECT_TRUE((a <=> b) == std::weak_ordering::less);
  EXPECT_TRUE((a <=> c) == std::weak_ordering::equivalent);
  EXPECT_TRUE((b <=> a) == std::weak_ordering::greater);
  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == b);
  // valueless handling for weak ordering
  indirect<Weak> valuelessSrc(std::in_place, Weak{1});
  indirect<Weak> holder = std::move(valuelessSrc);
  EXPECT_TRUE(valuelessSrc.valueless_after_move());
  EXPECT_TRUE((valuelessSrc <=> a) == std::weak_ordering::less);
  EXPECT_TRUE((a <=> valuelessSrc) == std::weak_ordering::greater);
}

TEST(Indirect, SynthesizedThreeWayFromLessOnly) {
  static_assert(!std::three_way_comparable<LessOnly>);
  indirect<LessOnly> a(std::in_place, LessOnly{1});
  indirect<LessOnly> b(std::in_place, LessOnly{2});
  indirect<LessOnly> c(std::in_place, LessOnly{1});
  static_assert(std::is_same_v<decltype(a <=> b), std::weak_ordering>);
  EXPECT_TRUE((a <=> b) == std::weak_ordering::less);
  EXPECT_TRUE((b <=> a) == std::weak_ordering::greater);
  EXPECT_TRUE((a <=> c) == std::weak_ordering::equivalent);
  // mixed indirect vs T, and a valueless operand ordering below a valued one
  EXPECT_TRUE((a <=> LessOnly{2}) == std::weak_ordering::less);
  auto valueless = makeValueless<LessOnly>(LessOnly{1});
  ASSERT_TRUE(valueless.valueless_after_move());
  EXPECT_TRUE((valueless <=> a) == std::weak_ordering::less);
}

TEST(Indirect, CopyAssignValuelessWithPropagatingAllocator) {
  StatefulAlloc<int, true, false, false> alloc1(1), alloc2(2);
  indirect<int, StatefulAlloc<int, true, false, false>> dst(
      std::allocator_arg, alloc1, std::in_place, 1);
  indirect<int, StatefulAlloc<int, true, false, false>> src(
      std::allocator_arg, alloc2, std::in_place, 2);
  // Make dst valueless via move
  auto holder = std::move(dst);
  EXPECT_TRUE(dst.valueless_after_move());
  // Copy-assign valued src into valueless dst with unequal propagating
  // allocators This previously destroyed null with wrong allocator
  dst = src;
  EXPECT_FALSE(dst.valueless_after_move());
  EXPECT_EQ(*dst, 2);
  EXPECT_EQ(dst.get_allocator().id, 2);
  // No cross-allocator deallocation should have been flagged by StatefulAlloc
  // registry
}

TEST(Indirect, CopyAssignFastPathPreservesAddressForString) {
  indirect<std::string> a(std::in_place, "hello");
  auto* old_addr = &*a;
  indirect<std::string> b(std::in_place, "world");
  a = b; // should assign through, same address
  EXPECT_EQ(&*a, old_addr);
  EXPECT_EQ(*a, "world");
}

TEST(Indirect, ConstructFromValue) {
  indirect<int> a(42);
  EXPECT_EQ(*a, 42);
  std::string lvalue = "hello";
  indirect<std::string> b(lvalue);
  EXPECT_EQ(*b, "hello");
  EXPECT_EQ(lvalue, "hello"); // copied, not moved
  indirect<std::string> c(std::move(lvalue));
  EXPECT_EQ(*c, "hello");
  // conversion from a type convertible to T
  indirect<std::string> d("literal");
  EXPECT_EQ(*d, "literal");
}

TEST(Indirect, ConstructFromValueWithAllocator) {
  std::allocator<std::string> alloc;
  const std::string lvalue = "hello";
  indirect<std::string> a(std::allocator_arg, alloc, lvalue);
  EXPECT_EQ(*a, "hello");
  indirect<std::string> b(std::allocator_arg, alloc, std::string("world"));
  EXPECT_EQ(*b, "world");
  // allocator-extended forwarding of multiple arguments needs in_place
  indirect<std::string> c(std::allocator_arg, alloc, std::in_place, 3, 'x');
  EXPECT_EQ(*c, "xxx");
}

TEST(Indirect, DeductionGuide) {
  folly::indirect a(42);
  static_assert(std::is_same_v<decltype(a), indirect<int>>);
  EXPECT_EQ(*a, 42);
  folly::indirect b(std::string("hello"));
  static_assert(std::is_same_v<decltype(b), indirect<std::string>>);
  EXPECT_EQ(*b, "hello");
}

TEST(Indirect, ConstructibilityTraits) {
  struct NoDefault {
    NoDefault() = delete;
    explicit NoDefault(int) {}
  };
  // default-constructibility of T is a Mandates too, so the trait is true and
  // default-constructing indirect<NoDefault> fails the static assertion
  static_assert(std::is_default_constructible_v<indirect<NoDefault>>);
  static_assert(std::is_default_constructible_v<indirect<int>>);
  // a non-default-constructible T is still reachable through in_place
  indirect<NoDefault> noDefault(std::in_place, 1);
  EXPECT_FALSE(noDefault.valueless_after_move());
  // all value constructors are explicit
  static_assert(!std::is_convertible_v<int, indirect<int>>);
  static_assert(std::is_constructible_v<indirect<int>, int>);
  static_assert(std::is_nothrow_move_constructible_v<indirect<std::string>>);
}

TEST(Indirect, RvalueObserversMoveOut) {
  indirect<std::unique_ptr<int>> a(std::in_place, std::make_unique<int>(7));
  static_assert(
      std::is_same_v<decltype(*std::move(a)), std::unique_ptr<int>&&>);
  std::unique_ptr<int> taken = *std::move(a);
  EXPECT_EQ(*taken, 7);
  // the indirect still owns a (moved-from) value
  EXPECT_FALSE(a.valueless_after_move());
  EXPECT_EQ(*a, nullptr);

  indirect<std::unique_ptr<int>> b(std::in_place, std::make_unique<int>(9));
  std::unique_ptr<int> takenFromValue = *std::move(b);
  EXPECT_EQ(*takenFromValue, 9);
  EXPECT_FALSE(b.valueless_after_move());
}

TEST(Indirect, ConstObservers) {
  const indirect<std::string> a(std::in_place, "hello");
  static_assert(std::is_same_v<decltype(*a), const std::string&>);
  static_assert(std::is_same_v<decltype(a.operator->()), const std::string*>);
  EXPECT_EQ(*a, "hello");
  EXPECT_EQ(a.operator->(), &*a);
}

TEST(Indirect, AssignOnValueless) {
  indirect<std::string> src(std::in_place, "hello");
  indirect<std::string> sink = std::move(src);
  ASSERT_TRUE(src.valueless_after_move());
  src = std::string("restored");
  EXPECT_FALSE(src.valueless_after_move());
  EXPECT_EQ(*src, "restored");
}

TEST(Indirect, AssignFromOwnValue) {
  indirect<std::string> a(std::in_place, "hello");
  a = *a + " world";
  EXPECT_EQ(*a, "hello world");
  // argument aliases the value being assigned to
  indirect<std::string> b(std::in_place, "alias");
  b = *b;
  EXPECT_EQ(*b, "alias");
}

TEST(Indirect, AssignThrowOnValuelessStaysValueless) {
  indirect<ThrowOnCopy> src(std::in_place, 1);
  indirect<ThrowOnCopy> sink = std::move(src);
  ASSERT_TRUE(src.valueless_after_move());
  // assigning to a valueless indirect constructs a new owned object, so a
  // throwing copy leaves it valueless rather than half-formed
  EXPECT_THROW(src = ThrowOnCopy{42}, std::runtime_error);
  EXPECT_TRUE(src.valueless_after_move());
}

TEST(Indirect, CopyCtorThrowLeavesSourceIntact) {
  indirect<ThrowOnCopy> a(std::in_place, 42);
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
  EXPECT_THROW(indirect<ThrowOnCopy> b(a), std::runtime_error);
  EXPECT_EQ(a->v, 42);
}

TEST(Indirect, CopyCtorUsesSelectOnContainerCopyConstruction) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  indirect<int, Alloc> a(std::allocator_arg, Alloc(3), std::in_place, 5);
  indirect<int, Alloc> b = a;
  EXPECT_EQ(*b, 5);
  EXPECT_NE(&*a, &*b);
  EXPECT_EQ(b.get_allocator().id, 3);
}

TEST(Indirect, CopyCtorWithAllocatorFromValueless) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  indirect<int, Alloc> src(std::allocator_arg, Alloc(1), std::in_place, 5);
  indirect<int, Alloc> sink(std::allocator_arg, Alloc(1), std::move(src));
  ASSERT_TRUE(src.valueless_after_move());
  indirect<int, Alloc> copy(std::allocator_arg, Alloc(2), src);
  EXPECT_TRUE(copy.valueless_after_move());
  EXPECT_EQ(copy.get_allocator().id, 2);
}

TEST(Indirect, CopyAssignToValuelessThrowStaysValueless) {
  // a valueless destination takes the allocate-new path, which gives the
  // strong guarantee: a throwing copy leaves it valueless, not half-formed
  auto dst = makeValueless<ThrowOnCopy>(1);
  indirect<ThrowOnCopy> src(std::in_place, 42);
  ASSERT_TRUE(dst.valueless_after_move());
  EXPECT_THROW(dst = src, std::runtime_error);
  EXPECT_TRUE(dst.valueless_after_move());
  EXPECT_EQ(src->v, 42);
}

TEST(Indirect, CopyAssignKeepsAllocatorWhenNotPropagating) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  indirect<int, Alloc> a(std::allocator_arg, Alloc(1), std::in_place, 1);
  indirect<int, Alloc> b(std::allocator_arg, Alloc(2), std::in_place, 2);
  a = b;
  EXPECT_EQ(*a, 2);
  EXPECT_EQ(a.get_allocator().id, 1);
  // valueless destination must allocate with its own allocator
  indirect<int, Alloc> sink(std::allocator_arg, Alloc(1), std::move(a));
  ASSERT_TRUE(a.valueless_after_move());
  a = b;
  EXPECT_EQ(*a, 2);
  EXPECT_EQ(a.get_allocator().id, 1);
}

TEST(Indirect, CopyAndMoveAssignBothValueless) {
  indirect<int> a(std::in_place, 1);
  indirect<int> b(std::in_place, 2);
  indirect<int> sinkA = std::move(a);
  indirect<int> sinkB = std::move(b);
  ASSERT_TRUE(a.valueless_after_move());
  ASSERT_TRUE(b.valueless_after_move());
  a = b;
  EXPECT_TRUE(a.valueless_after_move());
  a = std::move(b);
  EXPECT_TRUE(a.valueless_after_move());
}

TEST(Indirect, MoveAssignFromValueless) {
  indirect<int> a(std::in_place, 1);
  indirect<int> src(std::in_place, 2);
  indirect<int> sink = std::move(src);
  ASSERT_TRUE(src.valueless_after_move());
  a = std::move(src);
  EXPECT_TRUE(a.valueless_after_move());
}

TEST(Indirect, MoveAssignFromValuelessPropagatesAllocator) {
  using Alloc = StatefulAlloc<int, false, true, false>;
  indirect<int, Alloc> a(std::allocator_arg, Alloc(1), std::in_place, 1);
  indirect<int, Alloc> src(std::allocator_arg, Alloc(2), std::in_place, 2);
  indirect<int, Alloc> sink(std::allocator_arg, Alloc(2), std::move(src));
  ASSERT_TRUE(src.valueless_after_move());
  a = std::move(src);
  EXPECT_TRUE(a.valueless_after_move());
  EXPECT_EQ(a.get_allocator().id, 2);
}

TEST(Indirect, SelfMoveAssign) {
  indirect<int> a(std::in_place, 5);
  auto& aref = a;
  a = std::move(aref);
  EXPECT_FALSE(a.valueless_after_move());
  EXPECT_EQ(*a, 5);
}

TEST(Indirect, AssignFromValueRequiresAssignableT) {
  // [indirect.assign] constrains operator=(U&&) on is_assignable_v<T&, U>, so
  // a copy-constructible but non-assignable T is not assignable from a value
  static_assert(!std::is_assignable_v<indirect<NonAssignable>&, NonAssignable>);
  static_assert(
      !std::is_assignable_v<indirect<NonAssignable>&, const NonAssignable&>);
}

TEST(Indirect, SwapEqualNonPropagatingAllocators) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  indirect<int, Alloc> a(std::allocator_arg, Alloc(1), std::in_place, 1);
  indirect<int, Alloc> b(std::allocator_arg, Alloc(1), std::in_place, 2);
  a.swap(b);
  EXPECT_EQ(*a, 2);
  EXPECT_EQ(*b, 1);
  // allocators are not swapped when propagate_on_container_swap is false
  EXPECT_EQ(a.get_allocator().id, 1);
  EXPECT_EQ(b.get_allocator().id, 1);
}

TEST(Indirect, SwapWithValueless) {
  indirect<int> a(std::in_place, 1);
  indirect<int> src(std::in_place, 2);
  indirect<int> sink = std::move(src);
  ASSERT_TRUE(src.valueless_after_move());
  a.swap(src);
  EXPECT_TRUE(a.valueless_after_move());
  EXPECT_EQ(*src, 1);
  // self swap is a no-op
  src.swap(src);
  EXPECT_EQ(*src, 1);
}

TEST(Indirect, MixedComparisonWithValueless) {
  indirect<int> src(std::in_place, 1);
  indirect<int> sink = std::move(src);
  ASSERT_TRUE(src.valueless_after_move());
  EXPECT_FALSE(src == 1);
  EXPECT_FALSE(1 == src);
  EXPECT_TRUE(src != 1);
  EXPECT_TRUE((src <=> 1) < 0);
  EXPECT_TRUE((1 <=> src) > 0);
}

TEST(Indirect, HashUsableInUnorderedContainer) {
  std::unordered_set<indirect<std::string>> set;
  set.emplace(std::in_place, "a");
  set.emplace(std::in_place, "b");
  set.emplace(std::in_place, "a");
  const std::unordered_set<indirect<std::string>> expected{
      indirect<std::string>(std::in_place, "a"),
      indirect<std::string>(std::in_place, "b")};
  EXPECT_EQ(set, expected);
}

TEST(Indirect, HashDisabledWithUnsupportedValueType) {
  struct Opaque {
    int v;
  };
  // [indirect.hash] enables hash iff hash<T> is enabled, so this is detectable
  static_assert(!std::is_default_constructible_v<std::hash<indirect<Opaque>>>);
  static_assert(std::is_default_constructible_v<std::hash<indirect<int>>>);
  // Comparison is not detectable the same way: [indirect.relops] states the
  // well-formedness of *lhs == *rhs as a Mandates, so operator== stays
  // declared for any T and comparing indirect<Opaque> is an error at the point
  // of use rather than an unsatisfied concept.
  static_assert(std::equality_comparable<indirect<int>>);
  static_assert(std::three_way_comparable<indirect<int>>);
}

#if defined(__cpp_lib_constexpr_dynamic_alloc) && \
    __cpp_lib_constexpr_dynamic_alloc >= 201907L
TEST(Indirect, ConstexprMutatingOperations) {
  static_assert([] {
    indirect<int> a(std::in_place, 1);
    indirect<int> b(std::in_place, 2);
    a.swap(b);
    a = 9;
    b = 5;
    indirect<int> c = std::move(b);
    return *a == 9 && *c == 5 && b.valueless_after_move() && a != c;
  }());
}
#endif

// Recursive type: indirect must be instantiable with an incomplete T. This is
// the primary motivation for the type.
struct Tree {
  int value{0};
  std::vector<indirect<Tree>> children;
  explicit Tree(int v) : value(v) {}
};

TEST(Indirect, RecursiveTypeDeepCopy) {
  Tree root(1);
  root.children.emplace_back(std::in_place, Tree(2));
  root.children.emplace_back(std::in_place, Tree(3));
  root.children[0]->children.emplace_back(std::in_place, Tree(4));

  Tree copy = root;
  copy.children[0]->children[0]->value = 99;
  EXPECT_EQ(root.children[0]->children[0]->value, 4);
  EXPECT_EQ(copy.children[0]->children[0]->value, 99);
  EXPECT_NE(&*root.children[0], &*copy.children[0]);
}

TEST(Indirect, HeterogeneousComparisons) {
  // indirect<int> vs indirect<long> - same value, different T
  indirect<int> ai(std::in_place, 5);
  indirect<long> al(std::in_place, 5L);
  EXPECT_TRUE(ai == al);
  EXPECT_TRUE((ai <=> al) == 0);
  al = 6L;
  EXPECT_FALSE(ai == al);
  EXPECT_TRUE((ai <=> al) < 0);
  EXPECT_TRUE((al <=> ai) > 0);

  // indirect vs U (bare value)
  EXPECT_TRUE(ai == 5);
  EXPECT_TRUE(ai == 5L);
  EXPECT_FALSE(ai == 6L);
  EXPECT_TRUE((ai <=> 5L) == 0);
  EXPECT_TRUE((ai <=> 6L) < 0);

  // valueless heterogeneous
  auto valuelessInt = makeValueless<int>(1);
  auto valuelessLong = makeValueless<long>(1L);
  EXPECT_TRUE(valuelessInt == valuelessLong);
  EXPECT_TRUE((valuelessInt <=> valuelessLong) == 0);
  EXPECT_FALSE(ai == valuelessLong);
  EXPECT_TRUE((valuelessInt <=> ai) < 0);
  EXPECT_TRUE((ai <=> valuelessInt) > 0);

  // different allocators, same value
  StatefulAlloc<int, false, false, false> alloc1(1), alloc2(2);
  indirect<int, StatefulAlloc<int, false, false, false>> a1(
      std::allocator_arg, alloc1, std::in_place, 7);
  indirect<int, StatefulAlloc<int, false, false, false>> a2(
      std::allocator_arg, alloc2, std::in_place, 7);
  EXPECT_TRUE(a1 == a2);
  EXPECT_TRUE((a1 <=> a2) == 0);
  a2 = makeValuelessAlloc<int, StatefulAlloc<int, false, false, false>>(
      alloc2, 1);
  EXPECT_FALSE(a1 == a2);
  EXPECT_TRUE((a2 <=> a1) < 0);
}

TEST(Indirect, MemberTypedefs) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  static_assert(std::is_same_v<indirect<int>::value_type, int>);
  static_assert(
      std::is_same_v<indirect<int>::allocator_type, std::allocator<int>>);
  static_assert(std::is_same_v<indirect<int>::pointer, int*>);
  static_assert(std::is_same_v<indirect<int>::const_pointer, const int*>);
  static_assert(std::is_same_v<indirect<int, Alloc>::allocator_type, Alloc>);
}

TEST(Indirect, AllocatorArgOnlyConstructor) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  indirect<int, Alloc> a(std::allocator_arg, Alloc(4));
  EXPECT_EQ(*a, 0);
  EXPECT_EQ(a.get_allocator().id, 4);
}

TEST(Indirect, DeductionGuideWithAllocator) {
  folly::indirect a(std::allocator_arg, std::allocator<int>{}, 42);
  static_assert(std::is_same_v<decltype(a), indirect<int>>);
  EXPECT_EQ(*a, 42);
  // the guide rebinds the allocator to the deduced value type
  folly::indirect b(std::allocator_arg, std::allocator<char>{}, 42);
  static_assert(
      std::is_same_v<decltype(b), indirect<int, std::allocator<int>>>);
  EXPECT_EQ(*b, 42);
}

TEST(Indirect, InitListWithTrailingArgs) {
  const std::vector<int> expected{1, 2, 3};
  indirect<std::vector<int>> a(std::in_place, {1, 2, 3}, std::allocator<int>{});
  EXPECT_EQ(*a, expected);
  indirect<std::vector<int>> b(
      std::allocator_arg,
      std::allocator<std::vector<int>>{},
      std::in_place,
      {1, 2, 3},
      std::allocator<int>{});
  EXPECT_EQ(*b, expected);
  const std::vector<int> reassigned{7, 8};
  a = std::vector<int>{7, 8};
  EXPECT_EQ(*a, reassigned);
}

TEST(Indirect, MoveCtorWithAllocatorFromValueless) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  auto src = makeValuelessAlloc<int, Alloc>(Alloc(1), 5);
  ASSERT_TRUE(src.valueless_after_move());
  indirect<int, Alloc> equalAlloc(std::allocator_arg, Alloc(1), std::move(src));
  EXPECT_TRUE(equalAlloc.valueless_after_move());
  EXPECT_EQ(equalAlloc.get_allocator().id, 1);

  auto src2 = makeValuelessAlloc<int, Alloc>(Alloc(1), 5);
  indirect<int, Alloc> unequalAlloc(
      std::allocator_arg, Alloc(2), std::move(src2));
  EXPECT_TRUE(unequalAlloc.valueless_after_move());
  EXPECT_EQ(unequalAlloc.get_allocator().id, 2);
}

TEST(Indirect, CopyAssignFromValuelessPropagatesAllocator) {
  using Alloc = StatefulAlloc<int, true, false, false>;
  indirect<int, Alloc> dst(std::allocator_arg, Alloc(1), std::in_place, 1);
  auto src = makeValuelessAlloc<int, Alloc>(Alloc(2), 2);
  ASSERT_TRUE(src.valueless_after_move());
  dst = src;
  EXPECT_TRUE(dst.valueless_after_move());
  EXPECT_EQ(dst.get_allocator().id, 2);
}

TEST(Indirect, SwapBothValueless) {
  auto a = makeValueless<int>(1);
  auto b = makeValueless<int>(2);
  ASSERT_TRUE(a.valueless_after_move());
  ASSERT_TRUE(b.valueless_after_move());
  swap(a, b);
  EXPECT_TRUE(a.valueless_after_move());
  EXPECT_TRUE(b.valueless_after_move());
}

TEST(Indirect, NoexceptSpecifications) {
  using Stateful = StatefulAlloc<int, false, false, false>;
  using PropMove = StatefulAlloc<int, false, true, false>;
  using PropSwap = StatefulAlloc<int, false, false, true>;

  // move construction is unconditionally noexcept
  static_assert(std::is_nothrow_move_constructible_v<indirect<int, Stateful>>);

  // allocator-extended move construction: noexcept iff is_always_equal
  static_assert(
      std::is_nothrow_constructible_v<
          indirect<int>,
          std::allocator_arg_t,
          const std::allocator<int>&,
          indirect<int>&&>);
  static_assert(
      !std::is_nothrow_constructible_v<
          indirect<int, Stateful>,
          std::allocator_arg_t,
          const Stateful&,
          indirect<int, Stateful>&&>);

  // move assignment: noexcept iff propagating or always-equal
  static_assert(std::is_nothrow_move_assignable_v<indirect<int>>);
  static_assert(std::is_nothrow_move_assignable_v<indirect<int, PropMove>>);
  static_assert(!std::is_nothrow_move_assignable_v<indirect<int, Stateful>>);

  // swap: noexcept iff propagating or always-equal
  static_assert(std::is_nothrow_swappable_v<indirect<int>>);
  static_assert(std::is_nothrow_swappable_v<indirect<int, PropSwap>>);
  static_assert(!std::is_nothrow_swappable_v<indirect<int, Stateful>>);

  // every observer is noexcept
  indirect<int> a(std::in_place, 1);
  const indirect<int>& ca = a;
  static_assert(noexcept(*a));
  static_assert(noexcept(*ca));
  static_assert(noexcept(*std::move(a)));
  static_assert(noexcept(a.operator->()));
  static_assert(noexcept(ca.operator->()));
  static_assert(noexcept(a.valueless_after_move()));
  static_assert(noexcept(a.get_allocator()));
}

TEST(Indirect, ConstRvalueDereference) {
  const indirect<std::string> a(std::in_place, "hello");
  using ConstRvalue = const indirect<std::string>&&;
  static_assert(
      std::is_same_v<
          decltype(*static_cast<ConstRvalue>(a)),
          const std::string&&>);
  EXPECT_EQ(*static_cast<ConstRvalue>(a), "hello");
}

TEST(Indirect, HeterogeneousComparisonAcrossAllocators) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  indirect<int> a(std::in_place, 5);
  indirect<int, Alloc> same(std::allocator_arg, Alloc(1), std::in_place, 5);
  indirect<int, Alloc> larger(std::allocator_arg, Alloc(1), std::in_place, 6);
  EXPECT_TRUE(a == same);
  EXPECT_FALSE(a == larger);
  EXPECT_TRUE((a <=> same) == 0);
  EXPECT_TRUE((a <=> larger) < 0);

  auto valueless = makeValuelessAlloc<int, Alloc>(Alloc(1), 5);
  ASSERT_TRUE(valueless.valueless_after_move());
  EXPECT_FALSE(a == valueless);
  EXPECT_TRUE((valueless <=> a) < 0);
  EXPECT_TRUE((a <=> valueless) > 0);
}

TEST(Indirect, PolymorphicAllocatorUsesAllocatorConstruction) {
  std::pmr::monotonic_buffer_resource pool;
  using Alloc = std::pmr::polymorphic_allocator<std::pmr::string>;
  indirect<std::pmr::string, Alloc> a(
      std::allocator_arg, Alloc(&pool), std::in_place, "hello");
  EXPECT_EQ(*a, "hello");
  // allocator_traits::construct performs uses-allocator construction, so the
  // owned string allocates from the same resource
  EXPECT_EQ(a->get_allocator().resource(), &pool);

  // select_on_container_copy_construction returns a default-constructed
  // polymorphic_allocator, so a copy must not inherit the pool
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
  indirect<std::pmr::string, Alloc> b = a;
  EXPECT_EQ(*b, "hello");
  EXPECT_EQ(b.get_allocator().resource(), std::pmr::get_default_resource());
}

TEST(Indirect, FancyPointerAllocator) {
  using Alloc = FancyAlloc<int>;
  static_assert(std::is_same_v<indirect<int, Alloc>::pointer, FancyPtr<int>>);
  static_assert(
      std::is_same_v<indirect<int, Alloc>::const_pointer, FancyPtr<const int>>);

  AllocCounters counters;
  {
    indirect<int, Alloc> a(
        std::allocator_arg, Alloc(&counters), std::in_place, 7);
    EXPECT_EQ(*a, 7);
    EXPECT_EQ(*a.operator->(), 7);
    const indirect<int, Alloc>& ca = a;
    EXPECT_EQ(*ca.operator->(), 7);

    indirect<int, Alloc> copy = a;
    *copy = 9;
    EXPECT_EQ(*a, 7);
    EXPECT_EQ(*copy, 9);

    indirect<int, Alloc> moved = std::move(a);
    EXPECT_TRUE(a.valueless_after_move());
    EXPECT_EQ(*moved, 7);
  }
  // the owned objects are created and destroyed through the allocator
  EXPECT_EQ(counters.constructed, 2);
  EXPECT_EQ(counters.destroyed, 2);
}

TEST(Indirect, MoveAssignNonMoveAssignableUnequalAllocators) {
  using Alloc = StatefulAlloc<NonAssignable, false, false, false>;
  indirect<NonAssignable, Alloc> a(
      std::allocator_arg, Alloc(1), std::in_place, 1);
  indirect<NonAssignable, Alloc> b(
      std::allocator_arg, Alloc(2), std::in_place, 2);
  a = std::move(b);
  EXPECT_EQ(a->value, 2);
  EXPECT_TRUE(b.valueless_after_move());
  EXPECT_EQ(a.get_allocator().id, 1);

  auto valueless = makeValuelessAlloc<NonAssignable, Alloc>(Alloc(1), 3);
  ASSERT_TRUE(valueless.valueless_after_move());
  indirect<NonAssignable, Alloc> src(
      std::allocator_arg, Alloc(2), std::in_place, 4);
  valueless = std::move(src);
  EXPECT_EQ(valueless->value, 4);
  EXPECT_EQ(valueless.get_allocator().id, 1);
}

TEST(Indirect, MoveAssignToValuelessUnequalAllocators) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  auto valueless = makeValuelessAlloc<int, Alloc>(Alloc(1), 1);
  ASSERT_TRUE(valueless.valueless_after_move());
  indirect<int, Alloc> src(std::allocator_arg, Alloc(2), std::in_place, 7);
  valueless = std::move(src);
  EXPECT_EQ(*valueless, 7);
  EXPECT_TRUE(src.valueless_after_move());
  EXPECT_EQ(valueless.get_allocator().id, 1);
}

TEST(Indirect, AssignFromConvertibleValue) {
  static_assert(std::is_assignable_v<indirect<std::string>&, std::string_view>);
  static_assert(std::is_assignable_v<indirect<long>&, int>);
  // std::string is assignable from int (through operator=(char)) but is not
  // constructible from it, so the overload must not accept it
  static_assert(!std::is_assignable_v<indirect<std::string>&, int>);
  static_assert(!std::is_assignable_v<indirect<int>&, indirect<long>>);

  indirect<std::string> s(std::in_place, "hello");
  const auto* addr = &*s;
  s = std::string_view("world");
  EXPECT_EQ(*s, "world");
  EXPECT_EQ(&*s, addr); // assigns through rather than reallocating

  auto valueless = makeValueless<std::string>("gone");
  ASSERT_TRUE(valueless.valueless_after_move());
  valueless = std::string_view("restored");
  EXPECT_FALSE(valueless.valueless_after_move());
  EXPECT_EQ(*valueless, "restored");
}

#ifndef NDEBUG
TEST(IndirectDeathTest, DereferencingValuelessAsserts) {
  indirect<int> src(std::in_place, 1);
  indirect<int> sink = std::move(src);
  ASSERT_TRUE(src.valueless_after_move());
  EXPECT_DEATH({ (void)*src; }, "");
  EXPECT_DEATH({ (void)src.operator->(); }, "");
}

TEST(IndirectDeathTest, SwapUnequalNonPropagatingAllocatorsAsserts) {
  using Alloc = StatefulAlloc<int, false, false, false>;
  indirect<int, Alloc> a(std::allocator_arg, Alloc(1), std::in_place, 1);
  indirect<int, Alloc> b(std::allocator_arg, Alloc(2), std::in_place, 2);
  EXPECT_DEATH({ a.swap(b); }, "");
}
#endif

// NOLINTEND(bugprone-use-after-move)
