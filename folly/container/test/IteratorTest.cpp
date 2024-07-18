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

#include <folly/container/Iterator.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <deque>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/portability/GTest.h>

#if defined(__cpp_lib_concepts)
#include <concepts>
#endif

class IteratorTest : public testing::Test {};

TEST_F(IteratorTest, iterator_has_known_distance_v) {
  EXPECT_FALSE((folly::iterator_has_known_distance_v<int*, int const*>));
  EXPECT_TRUE((folly::iterator_has_known_distance_v<int*, int*>));
}

TEST_F(IteratorTest, range_has_known_distance_v) {
  EXPECT_FALSE(folly::range_has_known_distance_v<std::list<int>&>);
  EXPECT_TRUE(folly::range_has_known_distance_v<std::vector<int>&>);
}

TEST_F(IteratorTest, iterator_category_t) {
  EXPECT_TRUE(( //
      std::is_same_v<
          folly::iterator_category_t<
              std::iterator<std::input_iterator_tag, int>>,
          std::input_iterator_tag>));
  EXPECT_FALSE(( //
      std::is_same_v<
          folly::iterator_category_t<
              std::iterator<std::input_iterator_tag, int>>,
          std::output_iterator_tag>));
}

TEST_F(IteratorTest, iterator_category_matches_v) {
  EXPECT_TRUE(( //
      folly::iterator_category_matches_v<
          std::iterator<std::input_iterator_tag, int>,
          std::input_iterator_tag>));
  EXPECT_FALSE(( //
      folly::iterator_category_matches_v<
          std::iterator<std::input_iterator_tag, int>,
          std::output_iterator_tag>));
  EXPECT_FALSE(( //
      folly::iterator_category_matches_v<int, std::input_iterator_tag>));
}

TEST_F(IteratorTest, iterator_value_type_t) {
  EXPECT_TRUE(( //
      std::is_same_v<
          std::map<int, double>::value_type,
          folly::iterator_value_type_t<std::map<int, double>::iterator>>));
  EXPECT_TRUE(( //
      std::is_same_v<
          std::map<std::reference_wrapper<int>, double&>::value_type,
          folly::iterator_value_type_t<
              std::map<std::reference_wrapper<int>, double&>::iterator>>));
  EXPECT_FALSE(( //
      std::is_same_v<
          std::map<int, float>::value_type,
          folly::iterator_value_type_t<std::map<int, double>::iterator>>));
}

TEST_F(IteratorTest, iterator_key_type_t) {
  EXPECT_TRUE(( //
      std::is_same_v<
          std::map<int, double>::key_type,
          folly::iterator_key_type_t<std::map<int, double>::iterator>>));
  EXPECT_TRUE(( //
      std::is_same_v<
          std::map<std::reference_wrapper<int>, double&>::key_type,
          folly::iterator_key_type_t<
              std::map<std::reference_wrapper<int>, double&>::iterator>>));
  EXPECT_TRUE(( //
      std::is_same_v<
          int,
          folly::iterator_key_type_t<std::iterator<
              std::input_iterator_tag,
              std::pair<const int&, double>>>>));
  EXPECT_FALSE(( //
      std::is_same_v<
          std::map<char, double>::key_type,
          folly::iterator_key_type_t<std::map<int, double>::iterator>>));
}

TEST_F(IteratorTest, iterator_mapped_type_t) {
  EXPECT_TRUE(( //
      std::is_same_v<
          std::map<int, double>::mapped_type,
          folly::iterator_mapped_type_t<std::map<int, double>::iterator>>));
  EXPECT_TRUE(( //
      std::is_same_v<
          std::map<std::reference_wrapper<int>, double&>::mapped_type,
          folly::iterator_mapped_type_t<
              std::map<std::reference_wrapper<int>, double&>::iterator>>));
  EXPECT_FALSE(( //
      std::is_same_v<
          std::map<int, float>::mapped_type,
          folly::iterator_mapped_type_t<std::map<int, double>::iterator>>));
}

namespace {
/**
 * Container type used for unit tests.
 */
template <typename T>
using Container = std::deque<T>;

// Constructor and assignment operator call counters for struct Object.
std::size_t gDefaultCtrCnt;
std::size_t gCopyCtrCnt;
std::size_t gMoveCtrCnt;
std::size_t gExplicitCtrCnt;
std::size_t gMultiargCtrCnt;
std::size_t gCopyOpCnt;
std::size_t gMoveOpCnt;
std::size_t gConvertOpCnt;

/**
 * Class that increases various counters to keep track of how objects have
 * been constructed or assigned to, to verify iterator behavior.
 */
struct Object {
  Object() { ++gDefaultCtrCnt; }
  Object(const Object&) { ++gCopyCtrCnt; }
  Object(Object&&) noexcept { ++gMoveCtrCnt; }
  explicit Object(int) { ++gExplicitCtrCnt; }
  explicit Object(int, int) { ++gMultiargCtrCnt; }
  Object& operator=(const Object&) {
    ++gCopyOpCnt;
    return *this;
  }
  Object& operator=(Object&&) noexcept {
    ++gMoveOpCnt;
    return *this;
  }
  Object& operator=(int) noexcept {
    ++gConvertOpCnt;
    return *this;
  }
};

/**
 * Reset all call counters to 0.
 */
void init_counters() {
  gDefaultCtrCnt = gCopyCtrCnt = gMoveCtrCnt = gExplicitCtrCnt =
      gMultiargCtrCnt = gCopyOpCnt = gMoveOpCnt = gConvertOpCnt = 0;
}

/**
 * Test for iterator copy and move.
 */
template <typename Iterator>
void copy_and_move_test(Container<int>& q, Iterator it) {
  assert(q.empty());
  const auto it2(it); // copy construct
  it = it2; // copy assign from const
  FOLLY_PUSH_WARNING
  FOLLY_CLANG_DISABLE_WARNING("-Wself-assign-overloaded")
  it = it; // self assign
  FOLLY_POP_WARNING
  auto it3(std::move(it)); // move construct
  it = std::move(it3); // move assign
  // Make sure iterator still works.
  it = 4711; // emplace
  EXPECT_EQ(q, Container<int>{4711});
}

/**
 * Test for emplacement with perfect forwarding.
 */
template <typename Iterator>
void emplace_test(Container<Object>& q, Iterator it) {
  using folly::make_emplace_args;
  assert(q.empty());
  init_counters();
  it = Object{}; // default construct + move construct
  Object obj; // default construct
  it = obj; // copy construct
  it = std::move(obj); // move construct
  const Object obj2; // default construct
  it = obj2; // copy construct from const
  it = std::move(obj2); // copy construct (const defeats move)
  it = 0; // explicit construct
  it = make_emplace_args(0, 0); // explicit multiarg construct
  it = std::make_pair(0, 0); // implicit multiarg construct
  it = std::make_tuple(0, 0); // implicit multiarg construct
  auto args = make_emplace_args(Object{}); // default construct + move construct
  it = args; // copy construct
  it = const_cast<const decltype(args)&>(args); // copy construct from const
  it = std::move(args); // move construct
  auto args2 = std::make_tuple(Object{}); // default construct + move construct
  it = args2; // (implicit multiarg) copy construct
  it = std::move(args2); // (implicit multiarg) move construct
  auto args3 = std::make_pair(0, 0);
  it = args3; // implicit multiarg construct
  it = std::move(args3); // implicit multiarg construct
  ASSERT_EQ(q.size(), 16);
  EXPECT_EQ(gDefaultCtrCnt, 5);
  EXPECT_EQ(gCopyCtrCnt, 6);
  EXPECT_EQ(gMoveCtrCnt, 6);
  EXPECT_EQ(gExplicitCtrCnt, 1);
  EXPECT_EQ(gMultiargCtrCnt, 5);
  EXPECT_EQ(gCopyOpCnt, 0);
  EXPECT_EQ(gMoveOpCnt, 0);
  EXPECT_EQ(gConvertOpCnt, 0);
}
} // namespace

using namespace folly;

/**
 * Basic tests for folly::emplace_iterator.
 */
TEST(EmplaceIterator, EmplacerTest) {
  {
    Container<int> q;
    copy_and_move_test(q, emplacer(q, q.begin()));
  }
  {
    Container<Object> q;
    emplace_test(q, emplacer(q, q.begin()));
  }
  {
    Container<int> q;
    auto it = emplacer(q, q.begin());
    it = 0;
    it = 1;
    it = 2;
    it = emplacer(q, q.begin());
    it = 3;
    it = 4;
    EXPECT_EQ(q, Container<int>({3, 4, 0, 1, 2}));
  }
}

/**
 * Basic tests for folly::front_emplace_iterator.
 */
TEST(EmplaceIterator, FrontEmplacerTest) {
  {
    Container<int> q;
    copy_and_move_test(q, front_emplacer(q));
  }
  {
    Container<Object> q;
    emplace_test(q, front_emplacer(q));
  }
  {
    Container<int> q;
    auto it = front_emplacer(q);
    it = 0;
    it = 1;
    it = 2;
    it = front_emplacer(q);
    it = 3;
    it = 4;
    EXPECT_EQ(q, Container<int>({4, 3, 2, 1, 0}));
  }
}

/**
 * Basic tests for folly::back_emplace_iterator.
 */
TEST(EmplaceIterator, BackEmplacerTest) {
  {
    Container<int> q;
    copy_and_move_test(q, back_emplacer(q));
  }
  {
    Container<Object> q;
    emplace_test(q, back_emplacer(q));
  }
  {
    Container<int> q;
    auto it = back_emplacer(q);
    it = 0;
    it = 1;
    it = 2;
    it = back_emplacer(q);
    it = 3;
    it = 4;
    EXPECT_EQ(q, Container<int>({0, 1, 2, 3, 4}));
  }
}

/**
 * Basic tests for folly::hint_emplace_iterator.
 */
TEST(EmplaceIterator, HintEmplacerTest) {
  {
    init_counters();
    std::map<int, Object> m;
    auto it = hint_emplacer(m, m.end());
    it = make_emplace_args(
        std::piecewise_construct,
        std::forward_as_tuple(0),
        std::forward_as_tuple(0));
    it = make_emplace_args(
        std::piecewise_construct,
        std::forward_as_tuple(1),
        std::forward_as_tuple(0, 0));
    it = make_emplace_args(
        std::piecewise_construct,
        std::forward_as_tuple(2),
        std::forward_as_tuple(Object{}));
    ASSERT_EQ(m.size(), 3);
    EXPECT_EQ(gDefaultCtrCnt, 1);
    EXPECT_EQ(gCopyCtrCnt, 0);
    EXPECT_EQ(gMoveCtrCnt, 1);
    EXPECT_EQ(gExplicitCtrCnt, 1);
    EXPECT_EQ(gMultiargCtrCnt, 1);
    EXPECT_EQ(gCopyOpCnt, 0);
    EXPECT_EQ(gMoveOpCnt, 0);
    EXPECT_EQ(gConvertOpCnt, 0);
  }
  {
    struct O {
      explicit O(int i_) : i(i_) {}
      bool operator<(const O& other) const { return i < other.i; }
      bool operator==(const O& other) const { return i == other.i; }
      int i;
    };
    std::vector<int> v1 = {0, 1, 2, 3, 4};
    std::vector<int> v2 = {0, 2, 4};
    std::set<O> diff;
    std::set_difference(
        v1.begin(),
        v1.end(),
        v2.begin(),
        v2.end(),
        hint_emplacer(diff, diff.end()));
    std::set<O> expected = {O(1), O(3)};
    ASSERT_EQ(diff, expected);
  }
}

/**
 * Test std::copy() with explicit conversion. This would not compile with a
 * std::back_insert_iterator, because the constructor of Object that takes a
 * single int is explicit.
 */
TEST(EmplaceIterator, Copy) {
  init_counters();
  Container<int> in({0, 1, 2});
  Container<Object> out;
  std::copy(in.begin(), in.end(), back_emplacer(out));
  EXPECT_EQ(3, out.size());
  EXPECT_EQ(gDefaultCtrCnt, 0);
  EXPECT_EQ(gCopyCtrCnt, 0);
  EXPECT_EQ(gMoveCtrCnt, 0);
  EXPECT_EQ(gExplicitCtrCnt, 3);
  EXPECT_EQ(gMultiargCtrCnt, 0);
  EXPECT_EQ(gCopyOpCnt, 0);
  EXPECT_EQ(gMoveOpCnt, 0);
  EXPECT_EQ(gConvertOpCnt, 0);
}

/**
 * Test std::transform() with multi-argument constructors. This would require
 * a temporary Object with std::back_insert_iterator.
 */
TEST(EmplaceIterator, Transform) {
  init_counters();
  Container<int> in({0, 1, 2});
  Container<Object> out;
  std::transform(in.begin(), in.end(), back_emplacer(out), [](int i) {
    return make_emplace_args(i, i);
  });
  EXPECT_EQ(3, out.size());
  EXPECT_EQ(gDefaultCtrCnt, 0);
  EXPECT_EQ(gCopyCtrCnt, 0);
  EXPECT_EQ(gMoveCtrCnt, 0);
  EXPECT_EQ(gExplicitCtrCnt, 0);
  EXPECT_EQ(gMultiargCtrCnt, 3);
  EXPECT_EQ(gCopyOpCnt, 0);
  EXPECT_EQ(gMoveOpCnt, 0);
  EXPECT_EQ(gConvertOpCnt, 0);
}

/**
 * Test multi-argument store and forward.
 */
TEST(EmplaceIterator, EmplaceArgs) {
  Object o1;
  const Object o2;
  Object& o3 = o1;
  const Object& o4 = o3;
  Object o5;

  {
    // Test copy construction.
    auto args = make_emplace_args(0, o1, o2, o3, o4, Object{}, std::cref(o2));
    init_counters();
    auto args2 = args;
    EXPECT_EQ(gDefaultCtrCnt, 0);
    EXPECT_EQ(gCopyCtrCnt, 5);
    EXPECT_EQ(gMoveCtrCnt, 0);
    EXPECT_EQ(gExplicitCtrCnt, 0);
    EXPECT_EQ(gMultiargCtrCnt, 0);
    EXPECT_EQ(gCopyOpCnt, 0);
    EXPECT_EQ(gMoveOpCnt, 0);
    EXPECT_EQ(gConvertOpCnt, 0);

    // Test copy assignment.
    init_counters();
    args = args2;
    EXPECT_EQ(gDefaultCtrCnt, 0);
    EXPECT_EQ(gCopyCtrCnt, 0);
    EXPECT_EQ(gMoveCtrCnt, 0);
    EXPECT_EQ(gExplicitCtrCnt, 0);
    EXPECT_EQ(gMultiargCtrCnt, 0);
    EXPECT_EQ(gCopyOpCnt, 5);
    EXPECT_EQ(gMoveOpCnt, 0);
    EXPECT_EQ(gConvertOpCnt, 0);
  }

  {
    // Test RVO.
    init_counters();
    auto args = make_emplace_args(
        0, o1, o2, o3, o4, Object{}, std::cref(o2), rref(std::move(o5)));
    EXPECT_EQ(gDefaultCtrCnt, 1);
    EXPECT_EQ(gCopyCtrCnt, 4);
    EXPECT_EQ(gMoveCtrCnt, 1);
    EXPECT_EQ(gExplicitCtrCnt, 0);
    EXPECT_EQ(gMultiargCtrCnt, 0);
    EXPECT_EQ(gCopyOpCnt, 0);
    EXPECT_EQ(gMoveOpCnt, 0);
    EXPECT_EQ(gConvertOpCnt, 0);

    // Test move construction.
    init_counters();
    auto args2 = std::move(args);
    EXPECT_EQ(gDefaultCtrCnt, 0);
    EXPECT_EQ(gCopyCtrCnt, 0);
    EXPECT_EQ(gMoveCtrCnt, 5);
    EXPECT_EQ(gExplicitCtrCnt, 0);
    EXPECT_EQ(gMultiargCtrCnt, 0);
    EXPECT_EQ(gCopyOpCnt, 0);
    EXPECT_EQ(gMoveOpCnt, 0);
    EXPECT_EQ(gConvertOpCnt, 0);

    // Test move assignment.
    init_counters();
    args = std::move(args2);
    EXPECT_EQ(gDefaultCtrCnt, 0);
    EXPECT_EQ(gCopyCtrCnt, 0);
    EXPECT_EQ(gMoveCtrCnt, 0);
    EXPECT_EQ(gExplicitCtrCnt, 0);
    EXPECT_EQ(gMultiargCtrCnt, 0);
    EXPECT_EQ(gCopyOpCnt, 0);
    EXPECT_EQ(gMoveOpCnt, 5);
    EXPECT_EQ(gConvertOpCnt, 0);

    // Make sure arguments are stored correctly. lvalues by reference, rvalues
    // by (moved) copy. Rvalues cannot be stored by reference because they may
    // refer to an expired temporary by the time they are accessed.
    static_assert(
        std::is_same<
            int,
            std::tuple_element_t<0, decltype(args)::storage_type>>::value,
        "");
    static_assert(
        std::is_same<
            Object,
            std::tuple_element_t<1, decltype(args)::storage_type>>::value,
        "");
    static_assert(
        std::is_same<
            Object,
            std::tuple_element_t<2, decltype(args)::storage_type>>::value,
        "");
    static_assert(
        std::is_same<
            Object,
            std::tuple_element_t<3, decltype(args)::storage_type>>::value,
        "");
    static_assert(
        std::is_same<
            Object,
            std::tuple_element_t<4, decltype(args)::storage_type>>::value,
        "");
    static_assert(
        std::is_same<
            Object,
            std::tuple_element_t<5, decltype(args)::storage_type>>::value,
        "");
    static_assert(
        std::is_same<
            std::reference_wrapper<const Object>,
            std::tuple_element_t<6, decltype(args)::storage_type>>::value,
        "");
    static_assert(
        std::is_same<
            rvalue_reference_wrapper<Object>,
            std::tuple_element_t<7, decltype(args)::storage_type>>::value,
        "");

    // Check whether args.get() restores the original argument type for
    // rvalue references to emplace_args.
    static_assert(
        std::is_same<int&&, decltype(get_emplace_arg<0>(std::move(args)))>::
            value,
        "");
    static_assert(
        std::is_same<Object&, decltype(get_emplace_arg<1>(std::move(args)))>::
            value,
        "");
    static_assert(
        std::is_same<
            const Object&,
            decltype(get_emplace_arg<2>(std::move(args)))>::value,
        "");
    static_assert(
        std::is_same<Object&, decltype(get_emplace_arg<3>(std::move(args)))>::
            value,
        "");
    static_assert(
        std::is_same<
            const Object&,
            decltype(get_emplace_arg<4>(std::move(args)))>::value,
        "");
    static_assert(
        std::is_same<Object&&, decltype(get_emplace_arg<5>(std::move(args)))>::
            value,
        "");
    static_assert(
        std::is_same<
            const Object&,
            decltype(get_emplace_arg<6>(std::move(args)))>::value,
        "");
    static_assert(
        std::is_same<Object&&, decltype(get_emplace_arg<7>(std::move(args)))>::
            value,
        "");

    // lvalue references to emplace_args should behave mostly like std::tuples.
    // Note that get_emplace_arg<7>(args) does not compile, because
    // folly::rvalue_reference_wrappers can only be unwrapped through an rvalue
    // reference.
    static_assert(
        std::is_same<int&, decltype(get_emplace_arg<0>(args))>::value, "");
    static_assert(
        std::is_same<Object&, decltype(get_emplace_arg<1>(args))>::value, "");
    static_assert(
        std::is_same<Object&, decltype(get_emplace_arg<2>(args))>::value, "");
    static_assert(
        std::is_same<Object&, decltype(get_emplace_arg<3>(args))>::value, "");
    static_assert(
        std::is_same<Object&, decltype(get_emplace_arg<4>(args))>::value, "");
    static_assert(
        std::is_same<Object&, decltype(get_emplace_arg<5>(args))>::value, "");
    static_assert(
        std::is_same<const Object&, decltype(get_emplace_arg<6>(args))>::value,
        "");
  }
}

/**
 * Test implicit unpacking.
 */
TEST(EmplaceIterator, ImplicitUnpack) {
  static std::size_t multiCtrCnt;
  static std::size_t pairCtrCnt;
  static std::size_t tupleCtrCnt;

  struct Object2 {
    Object2(int, int) { ++multiCtrCnt; }
    explicit Object2(const std::pair<int, int>&) { ++pairCtrCnt; }
    explicit Object2(const std::tuple<int, int>&) { ++tupleCtrCnt; }
  };

  auto test = [](auto&& it, bool expectUnpack) {
    multiCtrCnt = pairCtrCnt = tupleCtrCnt = 0;
    it = std::make_pair(0, 0);
    it = std::make_tuple(0, 0);
    if (expectUnpack) {
      EXPECT_EQ(multiCtrCnt, 2);
      EXPECT_EQ(pairCtrCnt, 0);
      EXPECT_EQ(tupleCtrCnt, 0);
    } else {
      EXPECT_EQ(multiCtrCnt, 0);
      EXPECT_EQ(pairCtrCnt, 1);
      EXPECT_EQ(tupleCtrCnt, 1);
    }
  };

  Container<Object2> q;

  test(emplacer(q, q.begin()), true);
  test(emplacer<false>(q, q.begin()), false);
  test(front_emplacer(q), true);
  test(front_emplacer<false>(q), false);
  test(back_emplacer(q), true);
  test(back_emplacer<false>(q), false);
}

// IndexIterator -------------

namespace index_iterator_type_tests {
namespace {

struct WeirdContainer {
  std::vector<bool> body_;

  using value_type = bool;
  using size_type = std::uint32_t;
  using difference_type = std::int32_t;

  auto operator[](size_type idx) { return body_[idx]; }
  auto operator[](size_type idx) const { return body_[idx]; }
};

// iterator concepts ------------------------
#if defined(__cpp_lib_concepts)

using vit = folly::index_iterator<std::vector<int>>;
using cvit = folly::index_iterator<const std::vector<int>>;
using vit_weird = folly::index_iterator<WeirdContainer>;
using cvit_weird = folly::index_iterator<const WeirdContainer>;

static_assert(std::random_access_iterator<vit>);
static_assert(std::random_access_iterator<cvit>);
static_assert(std::random_access_iterator<vit_weird>);
static_assert(std::random_access_iterator<cvit_weird>);

#endif

// dependent type computations ------------------------
struct ref_type {};
struct cref_type {};

struct just_operator {
  using value_type = int;
  ref_type operator[](std::size_t) { return {}; }
  cref_type operator[](std::size_t) const { return {}; }
};

struct has_size_type : just_operator {
  using size_type = std::uint32_t;
};

struct has_difference_type : just_operator {
  using difference_type = std::int32_t;
};

struct has_both_size_and_difference_type : just_operator {
  using size_type = std::uint32_t;
  using difference_type = std::int32_t;
};

template <typename T>
using all_types = std::tuple<
    typename std::iterator_traits<folly::index_iterator<T>>::value_type,
    typename std::iterator_traits<folly::index_iterator<T>>::reference,
    typename std::iterator_traits<folly::index_iterator<T>>::difference_type,
    typename folly::index_iterator<T>::size_type>;

// gives better error messages than std::is_same
template <typename T>
void is_same_test(T, T) {}

} // namespace

TEST(IndexIterator, Types) {
  is_same_test(
      all_types<just_operator>{},
      std::tuple<int, ref_type, std::ptrdiff_t, std::size_t>{});
  is_same_test(
      all_types<const just_operator>{},
      std::tuple<int, cref_type, std::ptrdiff_t, std::size_t>{});
  is_same_test(
      all_types<has_size_type>{},
      std::tuple<int, ref_type, std::ptrdiff_t, std::uint32_t>{});
  is_same_test(
      all_types<const has_size_type>{},
      std::tuple<int, cref_type, std::ptrdiff_t, std::uint32_t>{});
  is_same_test(
      all_types<has_difference_type>{},
      std::tuple<int, ref_type, std::int32_t, std::size_t>{});
  is_same_test(
      all_types<const has_difference_type>{},
      std::tuple<int, cref_type, std::int32_t, std::size_t>{});
  is_same_test(
      all_types<has_both_size_and_difference_type>{},
      std::tuple<int, ref_type, std::int32_t, std::uint32_t>{});
  is_same_test(
      all_types<const has_both_size_and_difference_type>{},
      std::tuple<int, cref_type, std::int32_t, std::uint32_t>{});
}

} // namespace index_iterator_type_tests

TEST(IndexIterator, Sort) {
  std::vector<int> v(100, 0);
  std::iota(v.rbegin(), v.rend(), 0);

  std::sort(
      index_iterator<std::vector<int>>{v, 0},
      index_iterator<std::vector<int>>{v, v.size()});

  std::vector<int> expected(100, 0);
  std::iota(expected.begin(), expected.end(), 0);
  ASSERT_EQ(expected, v);
}

namespace {

constexpr bool accessTest() {
  std::array<int, 2> a{0, 1};
  const std::array<int, 2> b{0, 1};

  const index_iterator<std::array<int, 2>> mutI{a, 0};
  const index_iterator<const std::array<int, 2>> constI{b, 0};

  if (*mutI != 0 || *constI != 0) {
    return false;
  }

  if (mutI[1] != 1 || constI[1] != 1) {
    return false;
  }

  return true;
}

static_assert(accessTest());

template <typename I>
constexpr bool mutationsTest(I i0, I i1) {
  auto tmp = i0;
  if (++tmp != i1) {
    return false;
  }
  tmp = i0;
  if (tmp++ != i0) {
    return false;
  }
  if (tmp != i1) {
    return false;
  }

  tmp = i1;
  if (--tmp != i0) {
    return false;
  }
  tmp = i1;
  if (tmp-- != i1) {
    return false;
  }
  if (tmp != i0) {
    return false;
  }

  return true;
}

static constexpr std::array<int, 2> kA{0, 1};

static constexpr index_iterator<const std::array<int, 2>> kI0{kA, 0};
static constexpr index_iterator<const std::array<int, 2>> kI1{kA, 1};

static_assert(kI0 == kI0);
static_assert(kI0 != kI1);
static_assert(kI0 < kI1);
static_assert(kI0 <= kI0);
static_assert(kI0 <= kI1);
static_assert(kI1 >= kI1);
static_assert(kI1 >= kI0);
static_assert(kI1 > kI0);

static_assert(mutationsTest(kI0, kI1));
static_assert(kI0 + 1 == kI1);
static_assert(kI1 - 1 == kI0);
static_assert(kI1 - kI0 == 1);
static_assert(kI0 - kI1 == -1);

struct IndexedVector {
  std::vector<int> v_;

  std::pair<int, int&> operator[](std::size_t i) {
    return {static_cast<int>(i), v_[i]};
  }

  std::pair<int, const int&> operator[](std::size_t i) const {
    return {static_cast<int>(i), v_[i]};
  }

  using value_type = std::pair<int, int>;

  using iterator = folly::index_iterator<IndexedVector>;
  using const_iterator = folly::index_iterator<const IndexedVector>;

  iterator begin() { return {*this, 0}; }
  const_iterator begin() const { return cbegin(); }
  const_iterator cbegin() const { return {*this, 0}; }

  iterator end() { return {*this, v_.size()}; }
  const_iterator end() const { return cend(); }
  const_iterator cend() const { return {*this, v_.size()}; }
};

} // namespace

TEST(IndexIterator, UseProxyReferences) {
  IndexedVector iv{{0, 1, 2, 3}};
  const IndexedVector& civ = iv;

  using it = decltype(iv.begin());
  using cit = decltype(civ.begin());

  using it_ref_t = typename std::iterator_traits<it>::reference;
  using it_cref_t = typename std::iterator_traits<cit>::reference;

  static_assert(std::is_same<it_ref_t, std::pair<int, int&>>::value, "");
  static_assert(std::is_same<it_cref_t, std::pair<int, const int&>>::value, "");

  static_assert(std::is_same<decltype(it {} -> first), int>::value, "");
  static_assert(std::is_same<decltype(it {} -> second), int&>::value, "");
  static_assert(std::is_same<decltype(cit {} -> first), int>::value, "");
  static_assert(
      std::is_same<decltype(cit {} -> second), const int&>::value, "");

  ASSERT_EQ(4, (std::count_if(civ.begin(), civ.end(), [](auto&& pair) {
              return pair.first == pair.second;
            })));
  ASSERT_EQ(4, (std::count_if(iv.begin(), iv.end(), [](auto&& pair) {
              return pair.first == pair.second;
            })));
  cit conversion = iv.begin() + 1;
  ASSERT_EQ(&iv, conversion.get_container());
  ASSERT_EQ(1, conversion.get_index());

  static_assert(!std::is_convertible<cit, it>::value);

  for (auto&& x : iv) {
    x.second = -1;
  }

  std::vector<int> expected{-1, -1, -1, -1};
  ASSERT_EQ(expected, iv.v_);

  // testing pointer proxies
  for (auto f = iv.begin(); f != iv.end(); ++f) {
    f->second = 1;
  }

  expected = {1, 1, 1, 1};
  ASSERT_EQ(expected, iv.v_);
}

TEST(IndexIterator, OperatorArrowForNonProxies) {
  using v_t = std::vector<std::array<int, 2>>;
  using iterator = folly::index_iterator<v_t>;

  static_assert(std::is_same<iterator::pointer, v_t::pointer>::value, "");

  v_t v;
  v.resize(3);
  iterator f{v, 0};
  iterator l{v, v.size()};

  f->at(0) = 1;
  (f + 1)->at(0) = 2;
  (f + 2)->at(0) = 3;

  v_t expected{{1, 0}, {2, 0}, {3, 0}};

  ASSERT_EQ(expected, v);
}
