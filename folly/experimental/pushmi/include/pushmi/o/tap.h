// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <cassert>
#include "extension_operators.h"
#include "../deferred.h"
#include "../single_deferred.h"
#include "../time_single_deferred.h"

namespace pushmi {

namespace detail {

PUSHMI_TEMPLATE(class SideEffects, class Out)
  (requires Receiver<SideEffects> && Receiver<Out>)
struct tap_ {
  SideEffects sideEffects;
  Out out;

  // side effect has no effect on the properties.
  using properties = properties_t<Out>;

  PUSHMI_TEMPLATE(class V, class UV = std::remove_reference_t<V>)
    (requires
      // SingleReceiver<SideEffects, const UV&> &&
      SingleReceiver<Out, V>)
  void value(V&& v) {
    ::pushmi::set_value(sideEffects, as_const(v));
    ::pushmi::set_value(out, (V&&) v);
  }
  PUSHMI_TEMPLATE(class E)
    (requires
      // NoneReceiver<SideEffects, const E&> &&
      NoneReceiver<Out, E>)
  void error(E e) noexcept {
    ::pushmi::set_error(sideEffects, as_const(e));
    ::pushmi::set_error(out, std::move(e));
  }
  void done() {
    ::pushmi::set_done(sideEffects);
    ::pushmi::set_done(out);
  }
};

PUSHMI_INLINE_VAR constexpr struct make_tap_fn {
  PUSHMI_TEMPLATE(class SideEffects, class Out)
    (requires Receiver<SideEffects> && Receiver<Out> &&
      Receiver<tap_<SideEffects, Out>, property_set_index_t<properties_t<Out>, is_silent<>>>)
  auto operator()(SideEffects se, Out out) const {
    return tap_<SideEffects, Out>{std::move(se), std::move(out)};
  }
} const make_tap {};

#if __NVCC__
#define PUSHMI_STATIC_ASSERT(...)
#elif __cpp_if_constexpr >= 201606
#define PUSHMI_STATIC_ASSERT static_assert
#else
#define PUSHMI_STATIC_ASSERT detail::do_assert
inline void do_assert(bool condition, char const*) {
  assert(condition);
}
#endif

struct tap_fn {
private:
  template <class In, class SideEffects>
  static auto impl(In in, SideEffects sideEffects) {
    PUSHMI_STATIC_ASSERT(
      ::pushmi::detail::deferred_requires_from<In, SideEffects,
        SenderTo<In, SideEffects, is_none<>>,
        SenderTo<In, SideEffects, is_single<>>,
        TimeSenderTo<In, SideEffects, is_single<>> >(),
        "'In' is not deliverable to 'SideEffects'");

    return ::pushmi::detail::deferred_from<In, SideEffects>(
      std::move(in),
      ::pushmi::detail::submit_transform_out<In>(
        out_impl<In, SideEffects>{std::move(sideEffects)}
      )
    );
  }

  template <class... AN>
  struct in_impl {
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) {
      return tap_fn::impl(
        std::move(in),
        ::pushmi::detail::out_from_fn<In>()(std::move(args_)));
    }
  };
  template <class In, class SideEffects>
  struct out_impl {
    SideEffects sideEffects_;
    PUSHMI_TEMPLATE (class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      PUSHMI_STATIC_ASSERT(
        ::pushmi::detail::deferred_requires_from<In, SideEffects,
          SenderTo<In, Out, is_none<>>,
          SenderTo<In, Out, is_single<>>,
          TimeSenderTo<In, Out, is_single<>> >(),
          "'In' is not deliverable to 'Out'");
      auto gang{::pushmi::detail::out_from_fn<In>()(
          detail::make_tap(sideEffects_, std::move(out)))};
      using Gang = decltype(gang);
      PUSHMI_STATIC_ASSERT(
        ::pushmi::detail::deferred_requires_from<In, SideEffects,
          SenderTo<In, Gang>,
          SenderTo<In, Gang, is_single<>>,
          TimeSenderTo<In, Gang, is_single<>> >(),
          "'In' is not deliverable to 'Out' & 'SideEffects'");
      return gang;
    }
  };
public:
  template <class... AN>
  auto operator()(AN... an) const  {
    return in_impl<AN...>{{std::move(an)...}};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::tap_fn tap{};
} // namespace operators

} // namespace pushmi
