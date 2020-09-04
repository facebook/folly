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

#include <folly/Traits.h>

#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/ScopeGuard.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace std;

struct T1 {}; // old-style IsRelocatable, below
struct T2 {}; // old-style IsRelocatable, below
struct T3 {
  typedef std::true_type IsRelocatable;
};
struct T5 : T3 {};

struct F1 {};
struct F2 {
  typedef int IsRelocatable;
};
struct F3 : T3 {
  typedef std::false_type IsRelocatable;
};
struct F4 : T1 {};

template <class>
struct A {};
struct B {};

namespace folly {
template <>
struct IsRelocatable<T1> : std::true_type {};
template <>
FOLLY_ASSUME_RELOCATABLE(T2);
} // namespace folly

TEST(Traits, scalars) {
  EXPECT_TRUE(IsRelocatable<int>::value);
  EXPECT_TRUE(IsRelocatable<bool>::value);
  EXPECT_TRUE(IsRelocatable<double>::value);
  EXPECT_TRUE(IsRelocatable<void*>::value);
}

TEST(Traits, containers) {
  EXPECT_FALSE(IsRelocatable<vector<F1>>::value);
  EXPECT_TRUE((IsRelocatable<pair<F1, F1>>::value));
  EXPECT_TRUE((IsRelocatable<pair<T1, T2>>::value));
}

TEST(Traits, original) {
  EXPECT_TRUE(IsRelocatable<T1>::value);
  EXPECT_TRUE(IsRelocatable<T2>::value);
}

TEST(Traits, typedefd) {
  EXPECT_TRUE(IsRelocatable<T3>::value);
  EXPECT_TRUE(IsRelocatable<T5>::value);
  EXPECT_FALSE(IsRelocatable<F2>::value);
  EXPECT_FALSE(IsRelocatable<F3>::value);
}

TEST(Traits, unset) {
  EXPECT_TRUE(IsRelocatable<F1>::value);
  EXPECT_TRUE(IsRelocatable<F4>::value);
}

TEST(Traits, bitAndInit) {
  EXPECT_TRUE(IsZeroInitializable<int>::value);
  EXPECT_FALSE(IsZeroInitializable<vector<int>>::value);
}

template <bool V>
struct Cond {
  template <typename K = std::string>
  static auto fun_std(std::conditional_t<V, K, std::string>&& arg) {
    return std::is_same<folly::remove_cvref_t<decltype(arg)>, std::string>{};
  }
  template <typename K = std::string>
  static auto fun_folly(folly::conditional_t<V, K, std::string>&& arg) {
    return std::is_same<folly::remove_cvref_t<decltype(arg)>, std::string>{};
  }
};

TEST(Traits, conditional) {
  using folly::conditional_t;
  EXPECT_TRUE((std::is_same<conditional_t<false, char, int>, int>::value));
  EXPECT_TRUE((std::is_same<conditional_t<true, char, int>, char>::value));

  EXPECT_TRUE(Cond<false>::fun_std("hello"));
  EXPECT_TRUE(Cond<true>::fun_std("hello"));
  EXPECT_TRUE(Cond<false>::fun_folly("hello"));
  EXPECT_FALSE(Cond<true>::fun_folly("hello"));
}

TEST(Trait, logicOperators) {
  static_assert(Conjunction<true_type>::value, "");
  static_assert(!Conjunction<false_type>::value, "");
  static_assert(is_same<Conjunction<true_type>::type, true_type>::value, "");
  static_assert(is_same<Conjunction<false_type>::type, false_type>::value, "");
  static_assert(Conjunction<true_type, true_type>::value, "");
  static_assert(!Conjunction<true_type, false_type>::value, "");

  static_assert(Disjunction<true_type>::value, "");
  static_assert(!Disjunction<false_type>::value, "");
  static_assert(is_same<Disjunction<true_type>::type, true_type>::value, "");
  static_assert(is_same<Disjunction<false_type>::type, false_type>::value, "");
  static_assert(Disjunction<true_type, true_type>::value, "");
  static_assert(Disjunction<true_type, false_type>::value, "");

  static_assert(!Negation<true_type>::value, "");
  static_assert(Negation<false_type>::value, "");
}

TEST(Traits, is_negative) {
  EXPECT_TRUE(folly::is_negative(-1));
  EXPECT_FALSE(folly::is_negative(0));
  EXPECT_FALSE(folly::is_negative(1));
  EXPECT_FALSE(folly::is_negative(0u));
  EXPECT_FALSE(folly::is_negative(1u));

  EXPECT_TRUE(folly::is_non_positive(-1));
  EXPECT_TRUE(folly::is_non_positive(0));
  EXPECT_FALSE(folly::is_non_positive(1));
  EXPECT_TRUE(folly::is_non_positive(0u));
  EXPECT_FALSE(folly::is_non_positive(1u));
}

TEST(Traits, relational) {
  // We test, especially, the edge cases to make sure we don't
  // trip -Wtautological-comparisons

  EXPECT_FALSE((folly::less_than<uint8_t, 0u, uint8_t>(0u)));
  EXPECT_FALSE((folly::less_than<uint8_t, 0u, uint8_t>(254u)));
  EXPECT_FALSE((folly::less_than<uint8_t, 255u, uint8_t>(255u)));
  EXPECT_TRUE((folly::less_than<uint8_t, 255u, uint8_t>(254u)));

  // Making sure signed to unsigned comparisons are not truncated.
  EXPECT_TRUE((folly::less_than<uint8_t, 0, int8_t>(-1)));
  EXPECT_TRUE((folly::less_than<uint16_t, 0, int16_t>(-1)));
  EXPECT_TRUE((folly::less_than<uint32_t, 0, int32_t>(-1)));
  EXPECT_TRUE((folly::less_than<uint64_t, 0, int64_t>(-1)));

  EXPECT_FALSE((folly::less_than<int8_t, -1, uint8_t>(0)));
  EXPECT_FALSE((folly::less_than<int16_t, -1, uint16_t>(0)));
  EXPECT_FALSE((folly::less_than<int32_t, -1, uint32_t>(0)));
  EXPECT_FALSE((folly::less_than<int64_t, -1, uint64_t>(0)));

  EXPECT_FALSE((folly::greater_than<uint8_t, 0u, uint8_t>(0u)));
  EXPECT_TRUE((folly::greater_than<uint8_t, 0u, uint8_t>(254u)));
  EXPECT_FALSE((folly::greater_than<uint8_t, 255u, uint8_t>(255u)));
  EXPECT_FALSE((folly::greater_than<uint8_t, 255u, uint8_t>(254u)));

  EXPECT_FALSE((folly::greater_than<uint8_t, 0, int8_t>(-1)));
  EXPECT_FALSE((folly::greater_than<uint16_t, 0, int16_t>(-1)));
  EXPECT_FALSE((folly::greater_than<uint32_t, 0, int32_t>(-1)));
  EXPECT_FALSE((folly::greater_than<uint64_t, 0, int64_t>(-1)));

  EXPECT_TRUE((folly::greater_than<int8_t, -1, uint8_t>(0)));
  EXPECT_TRUE((folly::greater_than<int16_t, -1, uint16_t>(0)));
  EXPECT_TRUE((folly::greater_than<int32_t, -1, uint32_t>(0)));
  EXPECT_TRUE((folly::greater_than<int64_t, -1, uint64_t>(0)));
}

#if FOLLY_HAVE_INT128_T

TEST(Traits, int128) {
  EXPECT_TRUE(
      (::std::is_same<::std::make_unsigned<__int128_t>::type, __uint128_t>::
           value));
  EXPECT_TRUE((
      ::std::is_same<::std::make_signed<__int128_t>::type, __int128_t>::value));
  EXPECT_TRUE(
      (::std::is_same<::std::make_unsigned<__uint128_t>::type, __uint128_t>::
           value));
  EXPECT_TRUE(
      (::std::is_same<::std::make_signed<__uint128_t>::type, __int128_t>::
           value));
  EXPECT_TRUE((::std::is_arithmetic<__int128_t>::value));
  EXPECT_TRUE((::std::is_arithmetic<__uint128_t>::value));
  EXPECT_TRUE((::std::is_integral<__int128_t>::value));
  EXPECT_TRUE((::std::is_integral<__uint128_t>::value));
  EXPECT_FALSE((::std::is_unsigned<__int128_t>::value));
  EXPECT_TRUE((::std::is_signed<__int128_t>::value));
  EXPECT_TRUE((::std::is_unsigned<__uint128_t>::value));
  EXPECT_FALSE((::std::is_signed<__uint128_t>::value));
  EXPECT_TRUE((::std::is_fundamental<__int128_t>::value));
  EXPECT_TRUE((::std::is_fundamental<__uint128_t>::value));
  EXPECT_TRUE((::std::is_scalar<__int128_t>::value));
  EXPECT_TRUE((::std::is_scalar<__uint128_t>::value));
}

#endif // FOLLY_HAVE_INT128_T

template <typename T, typename... Args>
void testIsRelocatable(Args&&... args) {
  if (!IsRelocatable<T>::value) {
    return;
  }

  // We use placement new on zeroed memory to avoid garbage subsections
  char vsrc[sizeof(T)] = {0};
  char vdst[sizeof(T)] = {0};
  char vcpy[sizeof(T)];

  T* src = new (vsrc) T(std::forward<Args>(args)...);
  SCOPE_EXIT {
    src->~T();
  };
  std::memcpy(vcpy, vsrc, sizeof(T));
  T deep(*src);
  T* dst = new (vdst) T(std::move(*src));
  SCOPE_EXIT {
    dst->~T();
  };

  EXPECT_EQ(deep, *dst);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
  EXPECT_EQ(deep, *reinterpret_cast<T*>(vcpy));
#pragma GCC diagnostic pop

  // This test could technically fail; however, this is what relocation
  // almost always means, so it's a good test to have
  EXPECT_EQ(std::memcmp(vcpy, vdst, sizeof(T)), 0);
}

TEST(Traits, actuallyRelocatable) {
  // Ensure that we test stack and heap allocation for strings with in-situ
  // capacity
  testIsRelocatable<std::string>("1");
  testIsRelocatable<std::string>(sizeof(std::string) + 1, 'x');

  testIsRelocatable<std::vector<char>>(5, 'g');
}

struct inspects_tag {
  template <typename T>
  std::false_type is_char(tag_t<T>) const {
    return {};
  }
  std::true_type is_char(tag_t<char>) const {
    return {};
  }
};

TEST(Traits, tag) {
  inspects_tag f;
  EXPECT_FALSE(f.is_char(tag_t<int>{}));
  EXPECT_TRUE(f.is_char(tag_t<char>{}));
#if __cplusplus >= 201703L
  EXPECT_FALSE(f.is_char(tag<int>));
  EXPECT_TRUE(f.is_char(tag<char>));
#endif
}

namespace {
// has_value_type<T>::value is true if T has a nested type `value_type`
template <class T, class = void>
struct has_value_type : std::false_type {};

template <class T>
struct has_value_type<T, folly::void_t<typename T::value_type>>
    : std::true_type {};

struct some_tag {};

template <typename T>
struct container {
  template <class... Args>
  container(
      folly::type_t<some_tag, decltype(T(std::declval<Args>()...))>,
      Args&&...) {}
};
} // namespace

TEST(Traits, void_t) {
  EXPECT_TRUE((::std::is_same<folly::void_t<>, void>::value));
  EXPECT_TRUE((::std::is_same<folly::void_t<int>, void>::value));
  EXPECT_TRUE((::std::is_same<folly::void_t<int, short>, void>::value));
  EXPECT_TRUE(
      (::std::is_same<folly::void_t<int, short, std::string>, void>::value));
  EXPECT_TRUE((::has_value_type<std::string>::value));
  EXPECT_FALSE((::has_value_type<int>::value));
}

TEST(Traits, type_t) {
  EXPECT_TRUE((::std::is_same<folly::type_t<float>, float>::value));
  EXPECT_TRUE((::std::is_same<folly::type_t<float, int>, float>::value));
  EXPECT_TRUE((::std::is_same<folly::type_t<float, int, short>, float>::value));
  EXPECT_TRUE(
      (::std::is_same<folly::type_t<float, int, short, std::string>, float>::
           value));
  EXPECT_TRUE((
      ::std::is_constructible<::container<std::string>, some_tag, std::string>::
          value));
  EXPECT_FALSE(
      (::std::is_constructible<::container<std::string>, some_tag, float>::
           value));
}

namespace {
template <typename T, typename V>
using detector_find = decltype(std::declval<T>().find(std::declval<V>()));
}

TEST(Traits, is_detected) {
  EXPECT_TRUE((folly::is_detected<detector_find, std::string, char>::value));
  EXPECT_FALSE((folly::is_detected<detector_find, double, char>::value));
}

TEST(Traits, is_detected_v) {
  EXPECT_TRUE((folly::is_detected_v<detector_find, std::string, char>));
  EXPECT_FALSE((folly::is_detected_v<detector_find, double, char>));
}

TEST(Traits, aligned_storage_for_t) {
  struct alignas(2) Foo {
    char data[4];
  };
  using storage = aligned_storage_for_t<Foo[4]>;
  EXPECT_EQ(16, sizeof(storage));
  EXPECT_EQ(2, alignof(storage));
  EXPECT_TRUE(std::is_trivial<storage>::value);
  EXPECT_TRUE(std::is_standard_layout<storage>::value);
  EXPECT_TRUE(std::is_pod<storage>::value); // pod = trivial + standard-layout
}

TEST(Traits, remove_cvref) {
  using folly::remove_cvref;
  using folly::remove_cvref_t;

  // test all possible c-ref qualifiers without volatile
  EXPECT_TRUE((std::is_same<remove_cvref_t<int>, int>::value));
  EXPECT_TRUE((std::is_same<remove_cvref<int>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int&&>, int>::value));
  EXPECT_TRUE((std::is_same<remove_cvref<int&&>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int&>, int>::value));
  EXPECT_TRUE((std::is_same<remove_cvref<int&>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int const>, int>::value));
  EXPECT_TRUE((std::is_same<remove_cvref<int const>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int const&>, int>::value));
  EXPECT_TRUE((std::is_same<remove_cvref<int const&>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int const&&>, int>::value));
  EXPECT_TRUE((std::is_same<remove_cvref<int const&&>::type, int>::value));

  // test all possible c-ref qualifiers with volatile
  EXPECT_TRUE((std::is_same<remove_cvref_t<int volatile>, int>::value));
  EXPECT_TRUE((std::is_same<remove_cvref<int volatile>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int volatile&&>, int>::value));
  EXPECT_TRUE((std::is_same<remove_cvref<int volatile&&>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int volatile&>, int>::value));
  EXPECT_TRUE((std::is_same<remove_cvref<int volatile&>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int volatile const>, int>::value));
  EXPECT_TRUE(
      (std::is_same<remove_cvref<int volatile const>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int volatile const&>, int>::value));
  EXPECT_TRUE(
      (std::is_same<remove_cvref<int volatile const&>::type, int>::value));

  EXPECT_TRUE((std::is_same<remove_cvref_t<int volatile const&&>, int>::value));
  EXPECT_TRUE(
      (std::is_same<remove_cvref<int volatile const&&>::type, int>::value));
}

TEST(Traits, like) {
  EXPECT_TRUE((std::is_same<like_t<int, char>, char>::value));
  EXPECT_TRUE((std::is_same<like_t<int const, char>, char const>::value));
  EXPECT_TRUE((std::is_same<like_t<int volatile, char>, char volatile>::value));
  EXPECT_TRUE(
      (std::is_same<like_t<int const volatile, char>, char const volatile>::
           value));
  EXPECT_TRUE((std::is_same<like_t<int&, char>, char&>::value));
  EXPECT_TRUE((std::is_same<like_t<int const&, char>, char const&>::value));
  EXPECT_TRUE(
      (std::is_same<like_t<int volatile&, char>, char volatile&>::value));
  EXPECT_TRUE(
      (std::is_same<like_t<int const volatile&, char>, char const volatile&>::
           value));
  EXPECT_TRUE((std::is_same<like_t<int&&, char>, char&&>::value));
  EXPECT_TRUE((std::is_same<like_t<int const&&, char>, char const&&>::value));
  EXPECT_TRUE(
      (std::is_same<like_t<int volatile&&, char>, char volatile&&>::value));
  EXPECT_TRUE(
      (std::is_same<like_t<int const volatile&&, char>, char const volatile&&>::
           value));
}

TEST(Traits, is_instantiation_of_v) {
  EXPECT_TRUE((detail::is_instantiation_of_v<A, A<int>>));
  EXPECT_FALSE((detail::is_instantiation_of_v<A, B>));
}

TEST(Traits, is_instantiation_of) {
  EXPECT_TRUE((detail::is_instantiation_of<A, A<int>>::value));
  EXPECT_FALSE((detail::is_instantiation_of<A, B>::value));
}

TEST(Traits, is_constexpr_default_constructible) {
  EXPECT_TRUE(is_constexpr_default_constructible_v<int>);
  EXPECT_TRUE(is_constexpr_default_constructible<int>{});

  struct Empty {};
  EXPECT_TRUE(is_constexpr_default_constructible_v<Empty>);
  EXPECT_TRUE(is_constexpr_default_constructible<Empty>{});

  struct NonTrivialDtor {
    ~NonTrivialDtor() {}
  };
  EXPECT_FALSE(is_constexpr_default_constructible_v<NonTrivialDtor>);
  EXPECT_FALSE(is_constexpr_default_constructible<NonTrivialDtor>{});

  struct ConstexprCtor {
    int x, y;
    constexpr ConstexprCtor() noexcept : x(7), y(11) {}
  };
  EXPECT_TRUE(is_constexpr_default_constructible_v<ConstexprCtor>);
  EXPECT_TRUE(is_constexpr_default_constructible<ConstexprCtor>{});

  struct NonConstexprCtor {
    int x, y;
    NonConstexprCtor() noexcept : x(7), y(11) {}
  };
  EXPECT_FALSE(is_constexpr_default_constructible_v<NonConstexprCtor>);
  EXPECT_FALSE(is_constexpr_default_constructible<NonConstexprCtor>{});

  struct NoDefaultCtor {
    constexpr NoDefaultCtor(int, int) noexcept {}
  };
  EXPECT_FALSE(is_constexpr_default_constructible_v<NoDefaultCtor>);
  EXPECT_FALSE(is_constexpr_default_constructible<NoDefaultCtor>{});
}
