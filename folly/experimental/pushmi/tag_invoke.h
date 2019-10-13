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

#pragma once

#include <numeric>

#include <folly/Traits.h>
#include <folly/experimental/pushmi/detail/if_constexpr.h>
#include <folly/experimental/pushmi/traits.h>

#include <folly/experimental/pushmi/forwards.h>

namespace folly {
namespace pushmi {

/// \cond
struct test_member_function_operator {
  void operator()() {}
};
/// \endcond

PUSHMI_CONCEPT_DEF(
    template(class T) //
    concept MemberFunctionOperatorUndefined,
    requires(T t)( //
        (void)t, //
        &detail::inherit<T, test_member_function_operator>::operator() //
        ) //
);

PUSHMI_CONCEPT_DEF(
    template(class T) //
    concept TagFunction,
    not MemberFunctionOperatorUndefined<T> && std::is_class<T>::value &&
        Semiregular<T> && not std::is_final<T>::value //
);

template <class T, class Void = void>
struct tag_invoke_category {};

template <class T>
struct tag_invoke_category<T, void_t<typename T::category_t>> {
  using category_t = typename T::category_t;
};

template <class T>
using category_t = typename tag_invoke_category<T>::category_t;

PUSHMI_CONCEPT_DEF(
    template(class T, class Category) //
    concept TagFunctionKind,
    TagFunction<T>&& DerivedFrom<category_t<T>, Category> //
);

template <class T, class Void = void>
struct tag_invoke_signature {};

template <class T>
struct tag_invoke_signature<T, void_t<typename T::signature_t>> {
  using signature_t = typename T::signature_t;
};

template <class T>
using signature_t = typename tag_invoke_signature<T>::signature_t;

PUSHMI_CONCEPT_DEF(
    template(class T) //
    concept TypedTagFunction,
    TagFunction<T>&& std::is_function<signature_t<T>>::value //
);

#if __cpp_concepts && __cpp_nontype_template_parameter_auto >= 201606
template <auto& P>
PUSHMI_PP_CONSTRAINED_USING(
    TagFunction<remove_cvref_t<decltype(P)>>,
    tag_of =,
    remove_cvref_t<decltype(P)>);

PUSHMI_CONCEPT_DEF(
    template(auto& P) //
    concept TagFunctionV, //
    TagFunction<tag_of<P>>);

PUSHMI_CONCEPT_DEF(
    template(auto& P) //
    concept TypedTagFunctionV,
    TypedTagFunction<tag_of<P>>);

PUSHMI_CONCEPT_DEF(
    template(class T, auto& P) //
    concept TagOf,
    TagFunction<T>&& TagFunctionV<P>&& Same<T, tag_of<P>>);

#define PUSHMI_TAG_OF(...) ::folly::pushmi::tag_of<__VA_ARGS__>
#define PUSHMI_TAG_FUNCTION_V(...) ::folly::pushmi::TagFunctionV<__VA_ARGS__>
#define PUSHMI_TYPED_TAG_FUNCTION_V(...) \
  ::folly::pushmi::TypedTagFunctionV<__VA_ARGS__>
#define PUSHMI_IS_TAG_OF(T, ...) ::folly::pushmi::tag_of<T, __VA_ARGS__>
#else

template <class P>
PUSHMI_PP_CONSTRAINED_USING(
    TagFunction<remove_cvref_t<P>>,
    tag_of =,
    remove_cvref_t<P>);

PUSHMI_CONCEPT_DEF(
    template(class P) //
    concept TagFunctionV, //
    TagFunction<tag_of<P>>);

PUSHMI_CONCEPT_DEF(
    template(class P) //
    concept TypedTagFunctionV,
    TypedTagFunction<tag_of<P>>);

PUSHMI_CONCEPT_DEF(
    template(class T, class P) //
    concept TagOf,
    TagFunction<T>&& TagFunctionV<P>&& Same<T, tag_of<P>>);

#define PUSHMI_TAG_OF(...) ::folly::pushmi::tag_of<decltype(__VA_ARGS__)>
#define PUSHMI_TAG_FUNCTION_V(...) ::folly::pushmi::TagFunctionV<decltype(__VA_ARGS__)>
#define PUSHMI_TYPED_TAG_FUNCTION_V(...) \
  ::folly::pushmi::TypedTagFunctionV<decltype(__VA_ARGS__)>
#define PUSHMI_IS_TAG_OF(T, ...) \
  ::folly::pushmi::tag_of<T, decltype(__VA_ARGS__)>
#endif

namespace imp_tag_invoke {
// template<class... Args>
// void tag_invoke(Args&&...) = delete;

template <class P, class T, class... Args>
using imp_tag_invoke_t =
    decltype(tag_invoke(P{}, std::declval<T>(), std::declval<Args>()...));

struct imp_cpo {
  constexpr imp_cpo() = default;

  // Free function lookup with tag dispatching
  PUSHMI_TEMPLATE(class P, class T, class... Args)
  (requires //
   requires( //
       tag_invoke(P{}, std::declval<T>(), std::declval<Args>()...) //
       ) && //
   TagFunction<P>) //
      constexpr auto
      operator()(P p, T&& t, Args&&... args) const
      -> imp_tag_invoke_t<P, T, Args...> {
    return tag_invoke(p, (T &&) t, (Args &&) args...);
  }
};
} // namespace imp_tag_invoke

namespace _ {
PUSHMI_INLINE_VAR constexpr imp_tag_invoke::imp_cpo const tag_invoke{};
}
using namespace _;

PUSHMI_CONCEPT_DEF(
    template(class P, class T, class... Args) //
    (concept TagInvocable)(P, T, Args...), //
    Invocable<imp_tag_invoke::imp_cpo, P, T, Args...>);

template <class P, class T, class... Args>
using tag_invoke_result_t = invoke_result_t<imp_tag_invoke::imp_cpo, P, T, Args...>;

PUSHMI_CONCEPT_DEF(
    template(class P, class Ret, class T, class... Args) //
    (concept TagInvocableR)(P, Ret, T, Args...), //
    TagInvocable<P, T, Args...>&&
        ExplicitlyConvertibleTo<tag_invoke_result_t<P, T, Args...>, Ret>);

template <
    class CPO,
    class T,
    class Sig = typename CPO::signature_t,
    class = void>
struct imp_is_applicable {
  static constexpr bool value = false;
};

template <class CPO, class T, class Ret, class... Args>
struct imp_is_applicable<
    CPO,
    T,
    Ret(Args...),
    std::enable_if_t<static_cast<bool>(
        Invocable<CPO, T&, Args...>&&
            ExplicitlyConvertibleTo<invoke_result_t<CPO, T&, Args...>, Ret>)>> {
  static constexpr bool value = true;
};
template <class CPO, class T, class Ret, class... Args>
struct imp_is_applicable<
    CPO,
    T,
    Ret(Args...) const,
    std::enable_if_t<static_cast<bool>(
        Invocable<CPO, T const&, Args...>&& ExplicitlyConvertibleTo<
            invoke_result_t<CPO, T const&, Args...>,
            Ret>)>> {
  static constexpr bool value = true;
};
template <class CPO, class T, class Ret, class... Args>
struct imp_is_applicable<
    CPO,
    T,
    Ret(Args...)&&,
    std::enable_if_t<static_cast<bool>(
        Invocable<CPO, T, Args...>&&
            ExplicitlyConvertibleTo<invoke_result_t<CPO, T, Args...>, Ret>)>> {
  static constexpr bool value = true;
};

/// \cond
PUSHMI_TEMPLATE(class Cpo, class T)
(requires //
 TypedTagFunction<Cpo>) //
    PUSHMI_INLINE_VAR constexpr bool imp_is_applicable_v =
        imp_is_applicable<Cpo, remove_cvref_t<T>>::value;

PUSHMI_CONCEPT_DEF(
    template(class Cpo, class T) //
    concept Applicable, //
    TypedTagFunction<Cpo>&& imp_is_applicable_v<Cpo, remove_cvref_t<T>>);

PUSHMI_CONCEPT_DEF(
    template(class T, class... Cpo) //
    (concept InDomainOf)(T, Cpo...), //
    And<Applicable<Cpo, remove_cvref_t<T>>...>);

//
// Derive from typed_tag_function to declare new TypedTagFunctions
//

template <class Sig>
struct typed_tag_function {
  static_assert(
      std::is_function<Sig>::value,
      "the signature for this tag_function must be a function signature");
  using signature_t = Sig;
};

//
// Derive from kinded_tag_function to declare new TagFunctionKinds
//

template <class Category>
struct kinded_tag_function {
  using category_t = Category;
};


//
// wrapped cpo
//

template <class Wrapped>
struct imp_wrapped_impl {
  template <class Data>
  Wrapped& operator()(Data& data) const {
    return tag_invoke(*this, data);
  }
  template <class Data>
  Wrapped const& operator()(Data const& data) const {
    return tag_invoke(*this, data);
  }
};

template <class Wrapped>
PUSHMI_INLINE_VAR constexpr imp_wrapped_impl<Wrapped> const wrapped{};

//
// model_of cpo
//

template <class Sig>
struct imp_model_of_impl {
  constexpr imp_model_of_impl() = default;

  template <class Cpo, class Model>
  constexpr auto operator()(Cpo cpo, Model& m) const {
    return tag_invoke(*this, cpo, m);
  }
};

template <class Sig>
PUSHMI_INLINE_VAR constexpr imp_model_of_impl<Sig> const model_of{};

/// \cond
// Build a abstract interface corresponding to a list of properties.

namespace detail {
template <class, class, class>
struct imp_vtable_impl;

template <class, class, class = void>
struct imp_vtable_impl_select;

template <class Data, class Cpo>
struct imp_vtable_impl_select<Data, Cpo, void_t<typename Cpo::signature_t>> {
  using type = imp_vtable_impl<Data, Cpo, typename Cpo::signature_t>;
};

template <class Data, class Cpo, class Ret, class... An>
struct imp_vtable_impl<Data, Cpo, Ret(An...)> {
  constexpr imp_vtable_impl() : f_(&noop) {}

  using signature_t = Ret(Cpo, Data&, An...);
  template <class Model>
  explicit constexpr imp_vtable_impl(Model m)
      : f_(model_of<signature_t>(Cpo{}, m)) {}

  static Ret noop(Cpo, Data&, An...) {
    std::terminate();
  }

  signature_t* f_;

  Ret operator()(Cpo p, Data& data, An... an) const {
    return f_(p, data, (An &&) an...);
  }
  explicit operator bool() const {
    return f_ != nullptr && f_ != &noop;
  }
};

template <class Data, class Cpo, class Ret, class... An>
struct imp_vtable_impl<Data, Cpo, Ret(An...) const> {
  constexpr imp_vtable_impl() : f_(&noop) {}

  using signature_t = Ret(Cpo, Data const&, An...);
  template <class Model>
  explicit constexpr imp_vtable_impl(Model m)
      : f_(model_of<signature_t>(Cpo{}, m)) {}

  static Ret noop(Cpo, Data const&, An...) {
    std::terminate();
  }

  signature_t* f_;

  Ret operator()(Cpo p, Data const& data, An... an) const {
    return f_(p, data, (An &&) an...);
  }
  explicit operator bool() const {
    return f_ != nullptr && f_ != &noop;
  }
};

template <class Data, class Cpo, class Ret, class... An>
struct imp_vtable_impl<Data, Cpo, Ret(An...) &&> {
  constexpr imp_vtable_impl() : f_(&noop) {}

  using signature_t = Ret(Cpo, Data&&, An...);
  template <class Model>
  explicit constexpr imp_vtable_impl(Model m)
      : f_(model_of<signature_t>(Cpo{}, m)) {}

  static Ret noop(Cpo, Data&&, An...) {
    std::terminate();
  }

  signature_t* f_;

  Ret operator()(Cpo p, Data&& data, An... an) const {
    return f_(p, std::move(data), (An &&) an...);
  }
  explicit operator bool() const {
    return f_ != nullptr && f_ != &noop;
  }
};

} // namespace detail

template <class Data, class Cpo>
using imp_vtable_impl_t = typename detail::imp_vtable_impl_select<Data, Cpo>::type;

template <class Data, class... Cpo>
struct imp_vtable {
  struct vtable_impl : imp_vtable_impl_t<Data, Cpo>... {
    constexpr vtable_impl() = default;
    template <class Model>
    explicit constexpr vtable_impl(Model m)
        : imp_vtable_impl_t<Data, Cpo>(m)... {}

    // using imp_vtable_impl_t<Data, Cpo>::operator()...;
    PUSHMI_TEMPLATE(class... An)
    (requires //
     Invocable<overload_fn<imp_vtable_impl_t<Data, Cpo>...>&, An...> //
     ) //
        auto
        operator()(An&&... an) const {
      auto fn =
          overload(static_cast<imp_vtable_impl_t<Data, Cpo> const&>(*this)...);
      return fn((An &&) an...);
    }

    explicit operator bool() const {
      bool valid[] = {imp_vtable_impl_t<Data, Cpo>::operator bool()...};
      return std::accumulate(
          std::begin(valid), std::end(valid), true, [](bool l, bool r) {
            return l && r;
          });
    }
  };
  using type = vtable_impl;
};

// Build a concrete type that implements the abstract interface corresponding
// to a list of properties.

// for gcc5 - types that declare tag_invoke friends must be in a nested namespace
// and only the type is lifted into the parent namespace
namespace imp_vtable_model {

template <class, class, class, class>
struct imp_vtable_model_impl;

template <class, class, class, class = void>
struct imp_vtable_model_impl_select;

template <class Wrapped, class Data, class Cpo>
struct imp_vtable_model_impl_select<
    Wrapped,
    Data,
    Cpo,
    void_t<typename Cpo::signature_t>> {
  using type =
      imp_vtable_model_impl<Wrapped, Data, Cpo, typename Cpo::signature_t>;
};

template <class Wrapped, class Data, class Cpo, class Ret, class... An>
struct imp_vtable_model_impl<Wrapped, Data, Cpo, Ret(An...)> {
  imp_vtable_model_impl() = default;
  template <class OCpo>
  static Ret f(OCpo, Data& data, An... an) {
    return Cpo{}(wrapped<Wrapped>(data), (An &&) an...);
  }
  template <class OSig, class OCpo>
  friend constexpr auto
  tag_invoke(imp_model_of_impl<OSig>, OCpo, const imp_vtable_model_impl&)
      -> std::enable_if_t<static_cast<bool>(ConvertibleTo<OCpo, Cpo>), OSig*> {
    return &imp_vtable_model_impl::f<OCpo>;
  }
};

template <class Wrapped, class Data, class Cpo, class Ret, class... An>
struct imp_vtable_model_impl<Wrapped, Data, Cpo, Ret(An...) const> {
  imp_vtable_model_impl() = default;
  template <class OCpo>
  static Ret f(OCpo, Data const& data, An... an) {
    return Cpo{}(wrapped<Wrapped>(data), (An &&) an...);
  }
  template <class OSig, class OCpo>
  friend constexpr auto
  tag_invoke(imp_model_of_impl<OSig>, OCpo, const imp_vtable_model_impl&)
      -> std::enable_if_t<static_cast<bool>(ConvertibleTo<OCpo, Cpo>), OSig*> {
    return &imp_vtable_model_impl::f<OCpo>;
  }
};

template <class Wrapped, class Data, class Cpo, class Ret, class... An>
struct imp_vtable_model_impl<Wrapped, Data, Cpo, Ret(An...) &&> {
  imp_vtable_model_impl() = default;
  template <class OCpo>
  static Ret f(OCpo, Data&& data, An... an) {
    return Cpo{}(std::move(wrapped<Wrapped>(data)), (An &&) an...);
  }
  using signature_t = Ret(Cpo, Data&&, An...);
  template <class OSig, class OCpo>
  friend constexpr auto
  tag_invoke(imp_model_of_impl<OSig>, OCpo, const imp_vtable_model_impl&)
      -> std::enable_if_t<static_cast<bool>(ConvertibleTo<OCpo, Cpo>), OSig*> {
    return &imp_vtable_model_impl::f<OCpo>;
  }
};

template <class Wrapped, class Data, class Cpo>
using imp_vtable_model_impl_t =
    typename imp_vtable_model_impl_select<Wrapped, Data, Cpo>::type;

template <class Wrapped, class Data, class... Cpo>
struct vtable_model {
  struct vtable_model_impl : imp_vtable_model_impl_t<Wrapped, Data, Cpo>... {
    vtable_model_impl() = default;
  };
  using type = vtable_model_impl;
};

} // namespace imp_vtable_model
using imp_vtable_model::vtable_model;

// for gcc5 - types that declare tag_invoke friends must be in a nested namespace
// and only the type is lifted into the parent namespace
namespace imp_insitu_base {

template <class Wrapped, class InsituType>
constexpr bool fits_insitu = sizeof(Wrapped) <= sizeof(InsituType);

template <class InsituType, class Wrapped>
constexpr bool check_model() {
  return fits_insitu<Wrapped, InsituType> &&
      std::is_nothrow_move_constructible<Wrapped>::value;
}

template <class Derived, class InsituType, class... Cpos>
struct insitu_base {
 private:
  using self_t = insitu_base<Derived, InsituType, Cpos...>;
  template <class T>
  using not_self_t = detail::not_self_t<T, self_t, Derived>;

 protected:
  template <class Insitu, class Wrapped>
  friend struct imp_insitu_op_select;

  union data {
    void* pobj_ = nullptr;
    std::aligned_union_t<0, InsituType> buffer_;
  } data_{};

  struct insitu_move_op : typed_tag_function<void(data&, data*)> {
    insitu_move_op() = default;

    PUSHMI_TEMPLATE(class T)
    (requires //
     TagInvocable<insitu_move_op&, T, data&, data*> //
     ) //
        void
        operator()(T&& t, data& src, data* dst) const {
      tag_invoke(*this, (T &&) t, src, dst);
    }

    PUSHMI_TEMPLATE(class Wrapped)
    (requires //
     Constructible<Derived, not_self_t<Wrapped>> //
     ) //
        friend void tag_invoke(insitu_move_op, Wrapped&, data& src, data* dst) {
      PUSHMI_IF_CONSTEXPR((check_model<InsituType, Wrapped>())( //
          if (dst) { //
            new (&dst->buffer_)
                Wrapped(std::move(*static_cast<remove_cvref_t<Wrapped>*>(
                    (void*)&src.buffer_))); //
          } //
          static_cast<remove_cvref_t<Wrapped> const*>((void*)&src.buffer_)
              ->~Wrapped(); //
          ) else( //
          if (dst) { //
            dst->pobj_ = std::exchange(src.pobj_, nullptr); //
          } //
          delete static_cast<remove_cvref_t<Wrapped> const*>(src.pobj_); //
          ));
    }
  };
  struct insitu_copy_op : typed_tag_function<void(data const&, data&) const> {
    insitu_copy_op() = default;

    PUSHMI_TEMPLATE(class T)
    (requires //
     TagInvocable<
         insitu_copy_op&,
         T const&,
         data const&,
         data&> //
     ) //
        void
        operator()(T const& t, data const& src, data& dst) const {
      tag_invoke(*this, (T &&) t, src, dst);
    }

    PUSHMI_TEMPLATE(class Wrapped)
    (requires //
     Constructible<Derived, not_self_t<Wrapped>> //
     ) //
        friend void tag_invoke(
            insitu_copy_op,
            Wrapped const&,
            data const& src,
            data& dst) {
      PUSHMI_IF_CONSTEXPR((check_model<InsituType, Wrapped>())( //
          new (&dst.buffer_)
              Wrapped(*static_cast<remove_cvref_t<Wrapped> const*>(
                  (void*)&src.buffer_)); //
          ) else( //
          dst.pobj_ = new Wrapped(
              *static_cast<remove_cvref_t<Wrapped> const*>(src.pobj_)); //
          ));
    }
  };

  PUSHMI_TEMPLATE(class Wrapped)
  (requires //
   Constructible<Derived, not_self_t<Wrapped>> //
   ) //
      friend Wrapped& tag_invoke(imp_wrapped_impl<Wrapped>, self_t& self) {
    PUSHMI_IF_CONSTEXPR_RETURN((check_model<InsituType, Wrapped>())( //
        return *reinterpret_cast<Wrapped*>(&self.data_.buffer_); //
        ) else( //
        return *reinterpret_cast<Wrapped*>(self.data_.pobj_); //
        ));
  }

  PUSHMI_TEMPLATE(class Wrapped)
  (requires //
   Constructible<Derived, not_self_t<Wrapped>> //
   ) //
      friend Wrapped
      const& tag_invoke(imp_wrapped_impl<Wrapped>, self_t const& self) {
    PUSHMI_IF_CONSTEXPR_RETURN((check_model<InsituType, Wrapped>())( //
        return *reinterpret_cast<Wrapped const*>(&self.data_.buffer_); //
        ) else( //
        return *reinterpret_cast<Wrapped const*>(self.data_.pobj_); //
        ));
  }

  using vtable =
      typename imp_vtable<Derived, insitu_move_op, insitu_copy_op, Cpos...>::type;
  static constexpr vtable const empty_{};
  vtable const* vtable_ = &empty_;

  PUSHMI_TEMPLATE(class Wrapped)
  (requires //
   Constructible<Derived, not_self_t<Wrapped>> //
   ) //
      void make_model(Wrapped obj) {
    using model_t = typename vtable_model<
        Wrapped,
        Derived,
        insitu_move_op,
        insitu_copy_op,
        Cpos...>::type;

    static constexpr vtable const model_{model_t{}};
    vtable_ = &model_;

    PUSHMI_IF_CONSTEXPR((check_model<InsituType, Wrapped>())( //
        new (&this->data_.buffer_) Wrapped(std::move(obj)); //
        ) else( //
        this->data_.pobj_ = new Wrapped(std::move(obj)); //
        ));
  }

 public:
  insitu_base() = default;
  static insitu_move_op move_op() {
    return {};
  }
  static insitu_copy_op copy_op() {
    return {};
  }
};

// Class static definitions:
template <class Derived, class InsituType, class... Cpos>
constexpr typename insitu_base<Derived, InsituType, Cpos...>::vtable const
    insitu_base<Derived, InsituType, Cpos...>::empty_;

} // namespace imp_insitu_base

using imp_insitu_base::insitu_base;
using imp_insitu_base::check_model;

//
// Derive from any_tag_invoke_base to define new type-erased types
//

// for gcc5 - types that declare tag_invoke friends must be in a nested namespace
// and only the type is lifted into the parent namespace
namespace imp_any_tag_invoke_base {
template <class Derived, typename... Cpos>
class any_tag_invoke_base : private insitu_base<Derived, void * [2], Cpos...> {
  using self_t = any_tag_invoke_base<Derived, Cpos...>;
  using insitu_t = insitu_base<Derived, void * [2], Cpos...>;
  using vtable = typename insitu_t::vtable;

 protected:
  template <class T>
  using not_self_t = detail::not_self_t<T, self_t, Derived>;

  // provide access to this from friend functions

  static self_t& get_this(Derived& e) {
    return static_cast<self_t&>(e);
  }
  static self_t const& get_this(Derived const& e) {
    return static_cast<self_t const&>(e);
  }

  template <class T>
  struct destruct {
    T* e;
    ~destruct() {
      if (e != nullptr) {
        insitu_t::move_op()(*e, self_t::get_this(*e).data_, nullptr);
        self_t::get_this(*e).vtable_ = &insitu_t::empty_;
      }
    }
  };

  // Override tag_invoke for those properties that can be dispatched through the
  // virtual interface built by vtable above.

  PUSHMI_TEMPLATE(class Cpo, class T, class... Args)
  (requires //
   TagFunction<Cpo>&& DerivedFrom<remove_cvref_t<T>, Derived>&&
       Invocable<vtable&, Cpo, T, Args...> //
   ) //
      friend auto tag_invoke(Cpo p, T&& e, Args&&... args)
          -> invoke_result_t<vtable&, Cpo, T, Args...> {
    destruct<remove_cvref_t<T>> unwind{
        (std::is_rvalue_reference<T&&>::value && not std::is_const<T&&>::value)
            ? const_cast<remove_cvref_t<T>*>(std::addressof(e))
            : nullptr};
    return (*(self_t::get_this(e).vtable_))(p, (T &&) e, (Args &&) args...);
  }

  // Override tag_invoke for those properties that need to cast from
  // derived to insitu_t

  PUSHMI_TEMPLATE(class Wrapped)
  (requires //
   Constructible<Derived, not_self_t<Wrapped>>&&
       TagInvocable<imp_wrapped_impl<Wrapped>, insitu_t&> //
   ) //
      friend Wrapped& tag_invoke(imp_wrapped_impl<Wrapped> cpo, Derived& self) {
    return tag_invoke(cpo, static_cast<insitu_t&>(self_t::get_this(self)));
  }

  PUSHMI_TEMPLATE(class Wrapped)
  (requires //
   Constructible<Derived, not_self_t<Wrapped>>&&
       TagInvocable<imp_wrapped_impl<Wrapped>, insitu_t const&> //
   ) //
      friend Wrapped
      const& tag_invoke(imp_wrapped_impl<Wrapped> cpo, Derived const& self) {
    return tag_invoke(cpo, static_cast<insitu_t const&>(self_t::get_this(self)));
  }

 public:
  ~any_tag_invoke_base() {
    if (*(this->vtable_)) {
      insitu_t::move_op()(static_cast<Derived&>(*this), this->data_, nullptr);
      this->vtable_ = &insitu_t::empty_;
    }
  }

  any_tag_invoke_base() = default;

  any_tag_invoke_base(any_tag_invoke_base&& that) noexcept : any_tag_invoke_base() {
    if (*(that.vtable_)) {
      insitu_t::move_op()(
          static_cast<Derived&>(that), that.data_, &this->data_);
      this->vtable_ = that.vtable_;
    }
  }
  any_tag_invoke_base(any_tag_invoke_base const& that) noexcept : any_tag_invoke_base() {
    if (*(that.vtable_)) {
      insitu_t::copy_op()(
          static_cast<Derived const&>(that), that.data_, this->data_);
      this->vtable_ = that.vtable_;
    }
  }

  any_tag_invoke_base& operator=(any_tag_invoke_base&& that) noexcept {
    if (*(this->vtable_)) {
      insitu_t::move_op()(static_cast<Derived&>(*this), this->data_, nullptr);
      this->vtable_ = &insitu_t::empty_;
    }
    if (*(that.vtable_)) {
      insitu_t::move_op()(
          static_cast<Derived&>(that), that.data_, &this->data_);
      this->vtable_ = that.vtable_;
    }
    return *this;
  }
  any_tag_invoke_base& operator=(any_tag_invoke_base const& that) noexcept {
    if (*(this->vtable_)) {
      insitu_t::move_op()(static_cast<Derived&>(*this), this->data_, nullptr);
      this->vtable_ = &insitu_t::empty_;
    }
    if (*(that.vtable_)) {
      insitu_t::copy_op()(
          static_cast<Derived const&>(that), that.data_, this->data_);
      this->vtable_ = that.vtable_;
    }
    return *this;
  }

  explicit operator bool() {
    return !!*(this->vtable_);
  }

  PUSHMI_TEMPLATE(class Wrapped)
  (requires InDomainOf<
      not_self_t<Wrapped>,
      Cpos...>) //
      explicit any_tag_invoke_base(Wrapped obj) noexcept(
          check_model<void * [2], not_self_t<Wrapped>>())
      : any_tag_invoke_base() {
    insitu_t::make_model(std::move(obj));
  }
};
} // namespace imp_any_tag_invoke_base
using imp_any_tag_invoke_base::any_tag_invoke_base;
} // namespace pushmi
} // namespace folly
