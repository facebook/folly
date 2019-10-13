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

#include <folly/experimental/pushmi/tag_invoke.h>
#include <folly/experimental/pushmi/detail/if_constexpr.h>

using namespace folly::pushmi;

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace testing;

namespace {

struct executor_tag {};

template <class... Cpos>
class any_ex : private any_tag_invoke_base<any_ex<Cpos...>, Cpos...>,
               public executor_tag {
  using base_t = any_tag_invoke_base<any_ex<Cpos...>, Cpos...>;
  friend base_t;

 public:
  any_ex() = default;
  using base_t::base_t;
  using base_t::operator bool;
};

enum struct foo_kind {
  small = -1,
  unspecified,
  big,
};

// A CPO for querying what kind of foo an executor reports.
namespace __foo {
template <foo_kind K>
using __kind = std::integral_constant<foo_kind, K>;

template <foo_kind K>
struct __enum;
struct __cpo;

PUSHMI_TEMPLATE(class K)
(requires //
 ConvertibleTo<K, foo_kind>) //
    struct __cpo_impl : typed_tag_function<K() const> {
  PUSHMI_TEMPLATE(
      class Ex,
      class CPO = __cpo,
      class Default = __enum<foo_kind::unspecified>)
  (requires //
   DerivedFrom<Ex, executor_tag>) //
      static constexpr auto __foo(PUSHMI_CEXP_MAYBE_UNUSED Ex const& ex) {
    PUSHMI_IF_CONSTEXPR_RETURN((TagInvocableR<CPO, K, Ex const&>)( //
        return tag_invoke(CPO{}, id(ex)); //
        ) else( //
        return id(ex), Default{}; //
        ));
  }

  PUSHMI_TEMPLATE(class Ex)
  (requires //
   ConvertibleTo<decltype(__cpo_impl::__foo(std::declval<Ex const&>())), K>&&
       DerivedFrom<Ex, executor_tag>) //
      constexpr auto
      operator()(Ex const& ex) const {
    return __cpo_impl::__foo(ex);
  }
};

template <foo_kind K>
struct __enum : __cpo_impl<__kind<K>>, __kind<K> {};

struct __cpo : __cpo_impl<foo_kind> {
  static PUSHMI_INLINE_VAR constexpr __enum<foo_kind::unspecified> const
      unspecified{};
  static PUSHMI_INLINE_VAR constexpr __enum<foo_kind::big> const big{};
  static PUSHMI_INLINE_VAR constexpr __enum<foo_kind::small> const small{};

  static_assert(
      unspecified != big && unspecified != small,
      "avoid unused variable warnings and errors reported when maybe unused is applied.");
};
} // namespace __foo

PUSHMI_INLINE_VAR constexpr __foo::__cpo foo{};

struct destroyer_impl : typed_tag_function<void() &&> {
  PUSHMI_TEMPLATE(class Ex)
  (requires //
   DerivedFrom<remove_cvref_t<Ex>, executor_tag> //
   ) //
      constexpr auto
      operator()(Ex&& ex) const {
    return tag_invoke(*this, (Ex &&) ex);
  }
};

PUSHMI_INLINE_VAR constexpr destroyer_impl destroyer{};

// Test an executor to see whether the kind of foo is known at compile-
// time.
PUSHMI_CONCEPT_DEF(
    template(class Ex, foo_kind K) //
    (concept _FooExecutor)(Ex, K), //
    ConvertibleTo<
        decltype(foo(std::declval<std::decay_t<Ex> const&>())),
        __foo::__kind<K>>&& DerivedFrom<Ex, executor_tag>);

static_assert(
    lazy::_FooExecutor<executor_tag, foo_kind::unspecified>,
    "failed to use the concept");

static_assert(
    _FooExecutor<executor_tag, foo_kind::unspecified>,
    "failed to use the concept");

} // namespace

struct small_bar : executor_tag {
  void baz() {}

 private:
  friend constexpr auto tag_invoke(PUSHMI_TAG_OF(foo), const small_bar&) noexcept {
    return foo.small;
  }
};

struct big_bar : executor_tag {
  void baz() {}

 private:
  friend constexpr auto tag_invoke(PUSHMI_TAG_OF(foo), const big_bar&) noexcept {
    return foo.big;
  }
  friend constexpr auto tag_invoke(PUSHMI_TAG_OF(destroyer), big_bar&&) noexcept {}
};

static_assert(TagFunction<PUSHMI_TAG_OF(foo)>, "foo must be a tag_function");

static_assert(
    Same<PUSHMI_TAG_OF(foo(big_bar{})), PUSHMI_TAG_OF(foo.big)>,
    "foo(big_bar) must be foo.big");

static_assert(
    Same<PUSHMI_TAG_OF(foo(small_bar{})), PUSHMI_TAG_OF(foo.small)>,
    "foo(small_bar) must be foo.small");

static_assert(
    !InDomainOf<int&, PUSHMI_TAG_OF(foo)>,
    "foo must not be applicable to int");

static_assert(
    InDomainOf<executor_tag&, PUSHMI_TAG_OF(foo)>,
    "foo must be applicable to executor_tag");

static_assert(
    InDomainOf<big_bar&, PUSHMI_TAG_OF(foo)>,
    "foo must be applicable to big_bar");

static_assert(
    InDomainOf<small_bar&, PUSHMI_TAG_OF(foo)>,
    "foo must be applicable to small_bar");

static_assert(
    InDomainOf<any_ex<PUSHMI_TAG_OF(foo)>&, PUSHMI_TAG_OF(foo)>,
    "foo must be applicable to any_ex<foo>");

static_assert(
    InDomainOf<any_ex<>&, PUSHMI_TAG_OF(foo)>,
    "foo must be applicable to any_ex<>");

static_assert(
    _FooExecutor<any_ex<>, foo_kind::unspecified>,
    "any_ex<> must be foo of kind unspecified at compile-time");

static_assert(
    _FooExecutor<executor_tag, foo_kind::unspecified>,
    "executor_tag must be foo of kind unspecified at compile-time");

static_assert(
    _FooExecutor<big_bar, foo_kind::big>,
    "big_bar must be foo of kind big at compile-time");

static_assert(
    _FooExecutor<small_bar, foo_kind::small>,
    "small_bar must be foo of kind small at compile-time");

static_assert(
    Constructible<any_ex<PUSHMI_TAG_OF(foo)>, small_bar>,
    "any_ex<PUSHMI_TAG_OF(foo)> must be constructible from small_bar");

static_assert(
    Constructible<any_ex<>, small_bar>,
    "any_ex<> must be constructible from small_bar");

static_assert(
    Constructible<any_ex<PUSHMI_TAG_OF(foo)>, big_bar>,
    "any_ex<PUSHMI_TAG_OF(foo)> must be constructible from big_bar");

static_assert(
    Constructible<any_ex<>, big_bar>,
    "any_ex<> must be constructible from big_bar");

static_assert(
    !Constructible<any_ex<PUSHMI_TAG_OF(foo)>, int>,
    "any_ex<PUSHMI_TAG_OF(foo)> must not be constructible from int");

static_assert(
    Invocable<PUSHMI_TAG_OF(foo), any_ex<PUSHMI_TAG_OF(foo)>>,
    "foo must be invocable with any_ex<PUSHMI_TAG_OF(foo)>");

static_assert(
    TagInvocable<PUSHMI_TAG_OF(foo), any_ex<PUSHMI_TAG_OF(foo)>>,
    "foo must be pinvocable with any_ex<PUSHMI_TAG_OF(foo)>");

TEST(TagInvokeTest, TagInvokeFooOnTypeErasedSmallBar) {
  auto af0 = any_ex<PUSHMI_TAG_OF(foo)>{};
  EXPECT_THAT((!!af0), Eq(false));

  af0 = any_ex<PUSHMI_TAG_OF(foo)>{small_bar{}};
  EXPECT_THAT((!!af0), Eq(true));

  EXPECT_THAT((foo(af0)), Eq(foo.small));
  EXPECT_THAT((!!af0), Eq(true));
}

TEST(TagInvokeTest, TagInvokeFooOnTypeErasedBigBar) {
  auto af0 = any_ex<PUSHMI_TAG_OF(foo)>{big_bar{}};

  EXPECT_THAT((foo(af0)), Eq(foo.big));
}

TEST(TagInvokeTest, TagInvokeDestroyerOnTypeErasedBigBa) {
  auto af0 = any_ex<PUSHMI_TAG_OF(foo), PUSHMI_TAG_OF(destroyer)>{big_bar{}};

  EXPECT_THAT((foo(af0)), Eq(foo.big));
  destroyer(std::move(af0));
  EXPECT_THAT((!!af0), Eq(false));
}
