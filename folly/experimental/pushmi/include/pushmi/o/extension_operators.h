// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <tuple>
#include "../piping.h"
#include "../boosters.h"
#include "../single.h"
#include "../deferred.h"
#include "../single_deferred.h"
#include "../many.h"
#include "../many_deferred.h"
#include "../time_single_deferred.h"
#include "../flow_single.h"
#include "../flow_single_deferred.h"
#include "../detail/if_constexpr.h"
#include "../detail/functional.h"

namespace pushmi {

#if __cpp_lib_apply	>= 201603
using std::apply;
#else
namespace detail {
  PUSHMI_TEMPLATE (class F, class Tuple, std::size_t... Is)
    (requires requires (
      pushmi::invoke(std::declval<F>(), std::get<Is>(std::declval<Tuple>())...)
    ))
  constexpr decltype(auto) apply_impl(F&& f, Tuple&& t, std::index_sequence<Is...>) {
    return pushmi::invoke((F&&) f, std::get<Is>((Tuple&&) t)...);
  }
  template <class Tuple_, class Tuple = std::remove_reference_t<Tuple_>>
  using tupidxs = std::make_index_sequence<std::tuple_size<Tuple>::value>;
} // namespace detail

PUSHMI_TEMPLATE (class F, class Tuple)
  (requires requires (
    detail::apply_impl(std::declval<F>(), std::declval<Tuple>(), detail::tupidxs<Tuple>{})
  ))
constexpr decltype(auto) apply(F&& f, Tuple&& t) {
  return detail::apply_impl((F&&) f, (Tuple&&) t, detail::tupidxs<Tuple>{});
}
#endif

namespace detail {

template <class... TagN>
struct make_receiver;
template <>
struct make_receiver<is_none<>, void> : construct_deduced<none> {};
template <>
struct make_receiver<is_single<>, void> : construct_deduced<single> {};
template <>
struct make_receiver<is_many<>, void> : construct_deduced<many> {};
template <>
struct make_receiver<is_single<>, is_flow<>> : construct_deduced<flow_single> {};

template <PUSHMI_TYPE_CONSTRAINT(Sender) In>
struct out_from_fn {
  using Cardinality = property_set_index_t<properties_t<In>, is_silent<>>;
  using Flow = std::conditional_t<property_query_v<properties_t<In>, is_flow<>>, is_flow<>, void>;
  using Make = make_receiver<Cardinality, Flow>;
  PUSHMI_TEMPLATE (class... Ts)
   (requires Invocable<Make, Ts...>)
  auto operator()(std::tuple<Ts...> args) const {
    return pushmi::apply(Make(), std::move(args));
  }
  PUSHMI_TEMPLATE (class... Ts, class... Fns,
    class This = std::enable_if_t<sizeof...(Fns) != 0, out_from_fn>)
    (requires And<SemiMovable<Fns>...> &&
      Invocable<Make, std::tuple<Ts...>> &&
      Invocable<This, pushmi::invoke_result_t<Make, std::tuple<Ts...>>, Fns...>)
  auto operator()(std::tuple<Ts...> args, Fns...fns) const {
    return This()(This()(std::move(args)), std::move(fns)...);
  }
  PUSHMI_TEMPLATE(class Out, class...Fns)
    (requires Receiver<Out, Cardinality> && And<SemiMovable<Fns>...>)
  auto operator()(Out out, Fns... fns) const {
    return Make()(std::move(out), std::move(fns)...);
  }
};

PUSHMI_TEMPLATE(class In, class FN)
  (requires Sender<In> && SemiMovable<FN>
    PUSHMI_BROKEN_SUBSUMPTION(&& not TimeSender<In>))
auto submit_transform_out(FN fn){
  return on_submit(
    constrain(lazy::Receiver<_2>,
      [fn = std::move(fn)](In& in, auto out) {
        ::pushmi::submit(in, fn(std::move(out)));
      }
    )
  );
}

PUSHMI_TEMPLATE(class In, class FN)
  (requires TimeSender<In> && SemiMovable<FN>)
auto submit_transform_out(FN fn){
  return on_submit(
    constrain(lazy::Receiver<_3>,
      [fn = std::move(fn)](In& in, auto tp, auto out) {
        ::pushmi::submit(in, tp, fn(std::move(out)));
      }
    )
  );
}

PUSHMI_TEMPLATE(class In, class SDSF, class TSDSF)
  (requires Sender<In> && SemiMovable<SDSF> && SemiMovable<TSDSF>
    PUSHMI_BROKEN_SUBSUMPTION(&& not TimeSender<In>))
auto submit_transform_out(SDSF sdsf, TSDSF tsdsf) {
  return on_submit(
    constrain(lazy::Receiver<_2> && lazy::Invocable<SDSF&, In&, _2>,
      [sdsf = std::move(sdsf)](In& in, auto out) {
        sdsf(in, std::move(out));
      }
    )
  );
}

PUSHMI_TEMPLATE(class In, class SDSF, class TSDSF)
  (requires TimeSender<In> && SemiMovable<SDSF> && SemiMovable<TSDSF>)
auto submit_transform_out(SDSF sdsf, TSDSF tsdsf) {
  return on_submit(
    constrain(lazy::Receiver<_3> && lazy::Invocable<TSDSF&, In&, _2, _3>,
      [tsdsf = std::move(tsdsf)](In& in, auto tp, auto out) {
        tsdsf(in, tp, std::move(out));
      }
    )
  );
}

PUSHMI_TEMPLATE(class In)
  (requires Sender<In>)
auto deferred_from_maker() {
  PUSHMI_IF_CONSTEXPR_RETURN( ((bool) Sender<In, is_flow<>, is_single<>>) (
    return make_flow_single_deferred;
  ) else (
    PUSHMI_IF_CONSTEXPR_RETURN( ((bool) Sender<In, is_time<>, is_single<>>) (
      return make_time_single_deferred;
    ) else (
      PUSHMI_IF_CONSTEXPR_RETURN( ((bool) Sender<In, is_single<>>) (
        return make_single_deferred;
      ) else (
        PUSHMI_IF_CONSTEXPR_RETURN( ((bool) Sender<In, is_many<>>) (
          return make_many_deferred;
        ) else (
          return make_deferred;
        ))
      ))
    ))
  ))
}

PUSHMI_TEMPLATE(class In, class Out, class... FN)
  (requires Sender<In> && Receiver<Out>)
auto deferred_from(FN&&... fn) {
  return deferred_from_maker<In>()((FN&&) fn...);
}

PUSHMI_TEMPLATE(class In, class Out, class... FN)
  (requires Sender<In> && Receiver<Out>)
auto deferred_from(In in, FN&&... fn) {
  return deferred_from_maker<In>()(std::move(in), (FN&&) fn...);
}

PUSHMI_TEMPLATE(class In, class... FN)
  (requires Sender<In>)
auto deferred_from(FN&&... fn) {
  return deferred_from_maker<In>()((FN&&) fn...);
}

PUSHMI_TEMPLATE(class In, class... FN)
  (requires Sender<In>)
auto deferred_from(In in, FN&&... fn) {
  return deferred_from_maker<In>()(std::move(in), (FN&&) fn...);
}

PUSHMI_TEMPLATE(
    class In,
    class Out,
    bool SenderRequires,
    bool SingleSenderRequires,
    bool TimeSingleSenderRequires)
  (requires Sender<In> && Receiver<Out>)
constexpr bool deferred_requires_from() {
  PUSHMI_IF_CONSTEXPR_RETURN( ((bool) TimeSenderTo<In, Out, is_single<>>) (
    return TimeSingleSenderRequires;
  ) else (
    PUSHMI_IF_CONSTEXPR_RETURN( ((bool) SenderTo<In, Out, is_single<>>) (
      return SingleSenderRequires;
    ) else (
      PUSHMI_IF_CONSTEXPR_RETURN( ((bool) SenderTo<In, Out>) (
        return SenderRequires;
      ) else (
      ))
    ))
  ))
}

struct set_value_fn {
  template<class V>
  auto operator()(V&& v) const {
    return constrain(lazy::Receiver<_1, is_single<>>,
        [v = (V&&) v](auto out) mutable {
          ::pushmi::set_value(out, (V&&) v);
        }
    );
  }
};

struct set_error_fn {
  PUSHMI_TEMPLATE(class E)
    (requires SemiMovable<E>)
  auto operator()(E e) const {
    return constrain(lazy::NoneReceiver<_1, E>,
      [e = std::move(e)](auto out) mutable {
        ::pushmi::set_error(out, std::move(e));
      }
    );
  }
};

struct set_done_fn {
  auto operator()() const {
    return constrain(lazy::Receiver<_1>,
      [](auto out) {
        ::pushmi::set_done(out);
      }
    );
  }
};

struct set_next_fn {
  template<class V>
  auto operator()(V&& v) const {
    return constrain(lazy::Receiver<_1, is_many<>>,
        [v = (V&&) v](auto out) mutable {
          ::pushmi::set_next(out, (V&&) v);
        }
    );
  }
};

struct set_starting_fn {
  PUSHMI_TEMPLATE(class Up)
    (requires Receiver<Up>)
  auto operator()(Up up) const {
    return constrain(lazy::Receiver<_1>,
      [up = std::move(up)](auto out) {
        ::pushmi::set_starting(out, std::move(up));
      }
    );
  }
};

struct do_submit_fn {
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>)
  auto operator()(Out out) const {
    return constrain(lazy::SenderTo<_1, Out>,
      [out = std::move(out)](auto in) mutable {
        ::pushmi::submit(in, std::move(out));
      }
    );
  }
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Receiver<Out>)
  auto operator()(TP tp, Out out) const {
    return constrain(lazy::TimeSenderTo<_1, Out>,
      [tp = std::move(tp), out = std::move(out)](auto in) mutable {
        ::pushmi::submit(in, std::move(tp), std::move(out));
      }
    );
  }
};

struct now_fn {
  auto operator()() const {
    return constrain(lazy::TimeSender<_1>,
      [](auto in) {
        return ::pushmi::now(in);
      }
    );
  }
};

} // namespace detail

namespace extension_operators {

PUSHMI_INLINE_VAR constexpr detail::set_done_fn set_done{};
PUSHMI_INLINE_VAR constexpr detail::set_error_fn set_error{};
PUSHMI_INLINE_VAR constexpr detail::set_value_fn set_value{};
PUSHMI_INLINE_VAR constexpr detail::set_next_fn set_next{};
PUSHMI_INLINE_VAR constexpr detail::set_starting_fn set_starting{};
PUSHMI_INLINE_VAR constexpr detail::do_submit_fn submit{};
PUSHMI_INLINE_VAR constexpr detail::now_fn now{};
PUSHMI_INLINE_VAR constexpr detail::now_fn top{};

} // namespace extension_operators

} // namespace pushmi
