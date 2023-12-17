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

#include <folly/Traits.h>

#include <cstring>
#include <memory>
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

TEST(Traits, zeroInit) {
  // S1 is both trivially default-constructible and trivially
  // value-initializable. S2 is neither. S3 is trivially default-constructible
  // but not trivially value-initializable.
  struct S1 {
    int i_;
  };
  struct S2 {
    int i_ = 42;
  };
  struct S3 {
    int S1::*mp_;
  };

  EXPECT_TRUE(IsZeroInitializable<int>::value);
  EXPECT_TRUE(IsZeroInitializable<int*>::value);
  EXPECT_FALSE(IsZeroInitializable<vector<int>>::value);
  EXPECT_FALSE(IsZeroInitializable<S2>::value);
  EXPECT_FALSE(IsZeroInitializable<int S1::*>::value); // Itanium
  EXPECT_FALSE(IsZeroInitializable<S3>::value); // Itanium

  // TODO: S1 actually can be trivially zero-initialized, but
  // there's no portable way to distinguish it from S3, which can't.
  EXPECT_FALSE(IsZeroInitializable<S1>::value);
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

TEST(Traits, isNegative) {
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
      (::std::is_same<folly::make_unsigned_t<int128_t>, uint128_t>::value));
  EXPECT_TRUE(
      (::std::is_same<folly::make_signed_t<int128_t>, int128_t>::value));
  EXPECT_TRUE(
      (::std::is_same<folly::make_unsigned_t<uint128_t>, uint128_t>::value));
  EXPECT_TRUE( //
      (::std::is_same<folly::make_signed_t<uint128_t>, int128_t>::value));
  EXPECT_TRUE((folly::is_arithmetic_v<int128_t>));
  EXPECT_TRUE((folly::is_arithmetic_v<uint128_t>));
  EXPECT_TRUE((folly::is_integral_v<int128_t>));
  EXPECT_TRUE((folly::is_integral_v<uint128_t>));
  EXPECT_FALSE((folly::is_unsigned_v<int128_t>));
  EXPECT_TRUE((folly::is_signed_v<int128_t>));
  EXPECT_TRUE((folly::is_unsigned_v<uint128_t>));
  EXPECT_FALSE((folly::is_signed_v<__uint128_t>));
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
  SCOPE_EXIT { src->~T(); };
  std::memcpy(vcpy, vsrc, sizeof(T));
  T deep(*src);
  T* dst = new (vdst) T(std::move(*src));
  SCOPE_EXIT { dst->~T(); };

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
  std::true_type is_char(tag_t<char>) const { return {}; }
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

TEST(Traits, voidT) {
  EXPECT_TRUE((::std::is_same<folly::void_t<>, void>::value));
  EXPECT_TRUE((::std::is_same<folly::void_t<int>, void>::value));
  EXPECT_TRUE((::std::is_same<folly::void_t<int, short>, void>::value));
  EXPECT_TRUE(
      (::std::is_same<folly::void_t<int, short, std::string>, void>::value));
  EXPECT_TRUE((::has_value_type<std::string>::value));
  EXPECT_FALSE((::has_value_type<int>::value));
}

TEST(Traits, typeT) {
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

TEST(Traits, detectedOrT) {
  EXPECT_TRUE(( //
      std::is_same<
          folly::detected_or_t<float, detector_find, std::string, char>,
          std::string::size_type>::value));
  EXPECT_TRUE(( //
      std::is_same<
          folly::detected_or_t<float, detector_find, double, char>,
          float>::value));
}

TEST(Traits, detectedT) {
  EXPECT_TRUE(( //
      std::is_same<
          folly::detected_t<detector_find, std::string, char>,
          std::string::size_type>::value));
  EXPECT_TRUE(( //
      std::is_same<
          folly::detected_t<detector_find, double, char>,
          folly::nonesuch>::value));
}

TEST(Traits, isDetected) {
  EXPECT_TRUE((folly::is_detected<detector_find, std::string, char>::value));
  EXPECT_FALSE((folly::is_detected<detector_find, double, char>::value));
}

TEST(Traits, isDetectedV) {
  EXPECT_TRUE((folly::is_detected_v<detector_find, std::string, char>));
  EXPECT_FALSE((folly::is_detected_v<detector_find, double, char>));
}

TEST(Traits, fallbackIsNothrowConvertible) {
  EXPECT_FALSE((folly::fallback::is_nothrow_convertible<int, void>::value));
  EXPECT_TRUE((folly::fallback::is_nothrow_convertible<void, void>::value));
  struct foo {
    /* implicit */ FOLLY_MAYBE_UNUSED operator std::false_type();
    /* implicit */ FOLLY_MAYBE_UNUSED operator std::true_type() noexcept;
  };
  EXPECT_FALSE(
      (folly::fallback::is_nothrow_convertible<foo, std::false_type>::value));
  EXPECT_TRUE(
      (folly::fallback::is_nothrow_convertible<foo, std::true_type>::value));
}

TEST(Traits, isNothrowConvertible) {
  EXPECT_FALSE((folly::is_nothrow_convertible<int, void>::value));
  EXPECT_TRUE((folly::is_nothrow_convertible<void, void>::value));
  struct foo {
    /* implicit */ FOLLY_MAYBE_UNUSED operator std::false_type();
    /* implicit */ FOLLY_MAYBE_UNUSED operator std::true_type() noexcept;
  };
  EXPECT_FALSE((folly::is_nothrow_convertible<foo, std::false_type>::value));
  EXPECT_TRUE((folly::is_nothrow_convertible<foo, std::true_type>::value));
}

TEST(Traits, alignedStorageForT) {
  struct alignas(2) Foo {
    char data[4];
  };
  using storage = aligned_storage_for_t<Foo[4]>;
  EXPECT_EQ(16, sizeof(storage));
  EXPECT_EQ(2, alignof(storage));
  EXPECT_TRUE(std::is_trivial<storage>::value);
  EXPECT_TRUE(std::is_standard_layout<storage>::value);
}

TEST(Traits, removeCvref) {
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

TEST(Traits, isUnboundedArrayV) {
  EXPECT_FALSE((folly::is_unbounded_array_v<void>));
  EXPECT_FALSE((folly::is_unbounded_array_v<int>));
  EXPECT_TRUE((folly::is_unbounded_array_v<int[]>));
  EXPECT_FALSE((folly::is_unbounded_array_v<int[1]>));
}

TEST(Traits, isBoundedArrayV) {
  EXPECT_FALSE((folly::is_bounded_array_v<void>));
  EXPECT_FALSE((folly::is_bounded_array_v<int>));
  EXPECT_FALSE((folly::is_bounded_array_v<int[]>));
  EXPECT_TRUE((folly::is_bounded_array_v<int[1]>));
}

TEST(Traits, isInstantiationOfV) {
  EXPECT_TRUE((detail::is_instantiation_of_v<A, A<int>>));
  EXPECT_FALSE((detail::is_instantiation_of_v<A, B>));
}

TEST(Traits, isInstantiationOf) {
  EXPECT_TRUE((detail::is_instantiation_of<A, A<int>>::value));
  EXPECT_FALSE((detail::is_instantiation_of<A, B>::value));
}

TEST(Traits, isSimilarInstantiationV) {
  EXPECT_TRUE((detail::is_similar_instantiation_v<A<int>, A<long>>));
  EXPECT_FALSE((detail::is_similar_instantiation_v<A<int>, tag_t<int>>));
  EXPECT_FALSE((detail::is_similar_instantiation_v<A<int>, B>));
  EXPECT_FALSE((detail::is_similar_instantiation_v<B, B>));
}

TEST(Traits, isSimilarInstantiation) {
  EXPECT_TRUE((detail::is_similar_instantiation<A<int>, A<long>>::value));
  EXPECT_FALSE((detail::is_similar_instantiation<A<int>, tag_t<int>>::value));
  EXPECT_FALSE((detail::is_similar_instantiation<A<int>, B>::value));
  EXPECT_FALSE((detail::is_similar_instantiation<B, B>::value));
}

TEST(Traits, member_pointer_traits_data) {
  struct o {};
  using d = float;
  using mp_do = float o::*;
  using mp_traits_do = folly::member_pointer_traits<mp_do>;
  EXPECT_TRUE((std::is_same<d, mp_traits_do::member_type>::value));
  EXPECT_TRUE((std::is_same<o, mp_traits_do::object_type>::value));
}

TEST(Traits, member_pointer_traits_function) {
  struct o {};
  using f = float(char*) const;
  using mp_fo = float (o::*)(char*) const;
  using mp_traits_fo = folly::member_pointer_traits<mp_fo>;
  EXPECT_TRUE((std::is_same<f, mp_traits_fo::member_type>::value));
  EXPECT_TRUE((std::is_same<o, mp_traits_fo::object_type>::value));
}

TEST(Traits, isConstexprDefaultConstructible) {
  EXPECT_TRUE(is_constexpr_default_constructible_v<int>);
  EXPECT_TRUE(is_constexpr_default_constructible<int>{});

  struct Empty {};
  EXPECT_TRUE(is_constexpr_default_constructible_v<Empty>);
  EXPECT_TRUE(is_constexpr_default_constructible<Empty>{});

  struct NonTrivialDtor {
    FOLLY_MAYBE_UNUSED ~NonTrivialDtor() {}
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

TEST(Traits, uintBits) {
  EXPECT_TRUE((std::is_same_v<uint8_t, uint_bits_t<8>>));
  EXPECT_TRUE((std::is_same_v<uint16_t, uint_bits_t<16>>));
  EXPECT_TRUE((std::is_same_v<uint32_t, uint_bits_t<32>>));
  EXPECT_TRUE((std::is_same_v<uint64_t, uint_bits_t<64>>));
#if FOLLY_HAVE_INT128_T
  EXPECT_TRUE((std::is_same_v<uint128_t, uint_bits_t<128>>));
#endif // FOLLY_HAVE_INT128_T
}

TEST(Traits, uintBitsLg) {
  EXPECT_TRUE((std::is_same_v<uint8_t, uint_bits_lg_t<3>>));
  EXPECT_TRUE((std::is_same_v<uint16_t, uint_bits_lg_t<4>>));
  EXPECT_TRUE((std::is_same_v<uint32_t, uint_bits_lg_t<5>>));
  EXPECT_TRUE((std::is_same_v<uint64_t, uint_bits_lg_t<6>>));
#if FOLLY_HAVE_INT128_T
  EXPECT_TRUE((std::is_same_v<uint128_t, uint_bits_lg_t<7>>));
#endif // FOLLY_HAVE_INT128_T
}

TEST(Traits, intBits) {
  EXPECT_TRUE((std::is_same_v<int8_t, int_bits_t<8>>));
  EXPECT_TRUE((std::is_same_v<int16_t, int_bits_t<16>>));
  EXPECT_TRUE((std::is_same_v<int32_t, int_bits_t<32>>));
  EXPECT_TRUE((std::is_same_v<int64_t, int_bits_t<64>>));
#if FOLLY_HAVE_INT128_T
  EXPECT_TRUE((std::is_same_v<int128_t, int_bits_t<128>>));
#endif // FOLLY_HAVE_INT128_T
}

TEST(Traits, intBitsLg) {
  EXPECT_TRUE((std::is_same_v<int8_t, int_bits_lg_t<3>>));
  EXPECT_TRUE((std::is_same_v<int16_t, int_bits_lg_t<4>>));
  EXPECT_TRUE((std::is_same_v<int32_t, int_bits_lg_t<5>>));
  EXPECT_TRUE((std::is_same_v<int64_t, int_bits_lg_t<6>>));
#if FOLLY_HAVE_INT128_T
  EXPECT_TRUE((std::is_same_v<int128_t, int_bits_lg_t<7>>));
#endif // FOLLY_HAVE_INT128_T
}

struct type_pack_element_test {
  template <size_t I, typename... T>
  using fallback = traits_detail::type_pack_element_fallback<I, T...>;
  template <size_t I, typename... T>
  using native = type_pack_element_t<I, T...>;

  template <typename IC, typename... T>
  using fallback_ic = fallback<IC::value, T...>;
  template <typename IC, typename... T>
  using native_ic = native<IC::value, T...>;
};

TEST(Traits, typePackElementT) {
  using test = type_pack_element_test;

  EXPECT_TRUE(( //
      std::is_same_v<
          test::fallback<3, int, int, int, double, int, int>,
          double>));
  EXPECT_TRUE((std::is_same_v<test::fallback<0, int[1]>, int[1]>));
  EXPECT_TRUE((is_detected_v<test::fallback_ic, index_constant<0>, int>));
  EXPECT_FALSE((is_detected_v<test::fallback_ic, index_constant<0>>));

  EXPECT_TRUE(( //
      std::is_same_v<
          test::native<3, int, int, int, double, int, int>, //
          double>));
  EXPECT_TRUE((std::is_same_v<test::native<0, int[1]>, int[1]>));
  EXPECT_TRUE((is_detected_v<test::native_ic, index_constant<0>, int>));
  EXPECT_FALSE((is_detected_v<test::native_ic, index_constant<0>>));
}

TEST(Traits, isAllocator) {
  static_assert(is_allocator_v<std::allocator<int>>, "");
  static_assert(is_allocator<std::allocator<int>>::value, "");

  static_assert(is_allocator_v<std::allocator<std::string>>, "");
  static_assert(is_allocator<std::allocator<std::string>>::value, "");

  static_assert(!is_allocator_v<int>, "");
  static_assert(!is_allocator<int>::value, "");

  static_assert(!is_allocator_v<std::string>, "");
  static_assert(!is_allocator<std::string>::value, "");
}
