// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../piping.h"
#include "../boosters.h"
#include "../single.h"

namespace pushmi {

namespace detail{

template <class Tag>
struct make_receiver;
template <>
struct make_receiver<none_tag> : construct_deduced<none> {};
template <>
struct make_receiver<single_tag> : construct_deduced<single> {};

template <Sender In>
struct out_from_fn {
  using Make = make_receiver<sender_category_t<In>>;
  template <class... Ts>
   requires Invocable<Make, Ts...>
  auto operator()(std::tuple<Ts...> args) const {
    return std::apply(Make(), std::move(args));
  }
  template <class... Ts, class... Fns,
    class This = std::enable_if_t<sizeof...(Fns) != 0, out_from_fn>>
    requires (SemiMovable<Fns> &&...) &&
      Invocable<Make, std::tuple<Ts...>> &&
      Invocable<This, std::invoke_result_t<Make, std::tuple<Ts...>>, Fns...>
  auto operator()(std::tuple<Ts...> args, Fns...fns) const {
    return This()(This()(std::move(args)), std::move(fns)...);
  }
  template <Receiver<sender_category_t<In>> Out, class...Fns>
    requires (SemiMovable<Fns> &&...)
  auto operator()(Out out, Fns... fns) const {
    return Make()(std::move(out), std::move(fns)...);
  }
};

template<Sender In, class FN>
auto submit_transform_out(FN fn){
  if constexpr ((bool)TimeSender<In>) {
    return on_submit{
      [fn = std::move(fn)]<class TP, class Out>(In& in, TP tp, Out out) {
        ::pushmi::submit(in, tp, fn(std::move(out)));
      }
    };
  } else {
    return on_submit{
      [fn = std::move(fn)]<class Out>(In& in, Out out) {
        ::pushmi::submit(in, fn(std::move(out)));
      }
    };
  }
}

template<Sender In, class SDSF, class TSDSF>
auto submit_transform_out(SDSF sdsf, TSDSF tsdsf){
  if constexpr ((bool)TimeSender<In>) {
    return on_submit{
      [tsdsf = std::move(tsdsf)]<class TP, class Out>(In& in, TP tp, Out out) {
        tsdsf(in, tp, std::move(out));
      }
    };
  } else {
    return on_submit{
      [sdsf = std::move(sdsf)]<class Out>(In& in, Out out) {
        sdsf(in, std::move(out));
      }
    };
  }
}

template<Sender In, Receiver Out, class... FN>
auto deferred_from(FN&&... fn) {
  if constexpr ((bool)TimeSenderTo<In, Out, single_tag>) {
    return time_single_deferred{(FN&&) fn...};
  } else if constexpr ((bool)SenderTo<In, Out, single_tag>) {
    return single_deferred{(FN&&) fn...};
  } else if constexpr ((bool)SenderTo<In, Out>) {
    return deferred{(FN&&) fn...};
  }
}

template<Sender In, Receiver Out, class... FN>
auto deferred_from(In in, FN&&... fn) {
  if constexpr ((bool)TimeSenderTo<In, Out, single_tag>) {
    return time_single_deferred{std::move(in), (FN&&) fn...};
  } else if constexpr ((bool)SenderTo<In, Out, single_tag>) {
    return single_deferred{std::move(in), (FN&&) fn...};
  } else if constexpr ((bool)SenderTo<In, Out>) {
    return deferred{std::move(in), (FN&&) fn...};
  }
}

template<
    Sender In,
    Receiver Out,
    bool SenderRequires,
    bool SingleSenderRequires,
    bool TimeSingleSenderRequires>
constexpr bool deferred_requires_from() {
  if constexpr ((bool)TimeSenderTo<In, Out, single_tag>) {
    return TimeSingleSenderRequires;
  } else if constexpr ((bool)SenderTo<In, Out, single_tag>) {
    return SingleSenderRequires;
  } else if constexpr ((bool)SenderTo<In, Out>) {
    return SenderRequires;
  }
}

} // namespace detail

namespace extension_operators {

namespace detail{

struct set_value_fn {
  template<class V>
  auto operator()(V&& v) const {
    return [v = (V&&) v]<class Out>(Out out) mutable PUSHMI_VOID_LAMBDA_REQUIRES(Receiver<Out>) {
      ::pushmi::set_value(out, (V&&) v);
    };
  }
};

struct set_error_fn {
  template<class E>
  auto operator()(E e) const {
    return [e = std::move(e)]<class Out>(Out out) mutable noexcept PUSHMI_VOID_LAMBDA_REQUIRES(Receiver<Out>) {
      ::pushmi::set_error(out, std::move(e));
    };
  }
};

struct set_done_fn {
  auto operator()() const {
    return []<class Out>(Out out) PUSHMI_VOID_LAMBDA_REQUIRES(Receiver<Out>) {
      ::pushmi::set_done(out);
    };
  }
};

struct set_stopping_fn {
  auto operator()() const {
    return []<class Out>(Out out) PUSHMI_VOID_LAMBDA_REQUIRES(Receiver<Out>) {
      ::pushmi::set_stopping(out);
    };
  }
};

struct set_starting_fn {
  template<class Up>
  auto operator()(Up up) const {
    return [up = std::move(up)]<class Out>(Out out) PUSHMI_VOID_LAMBDA_REQUIRES(Receiver<Out>) {
      ::pushmi::set_starting(out, std::move(up));
    };
  }
};

struct submit_fn {
  template <class Out>
  auto operator()(Out out) const {
    static_assert(Receiver<Out>, "'Out' must be a model of Receiver");
    return [out = std::move(out)]<class In>(In in) mutable {
      ::pushmi::submit(in, std::move(out));
    };
  }
  template <class TP, class Out>
  auto operator()(TP tp, Out out) const {
    static_assert(Receiver<Out>, "'Out' must be a model of Receiver");
    return [tp = std::move(tp), out = std::move(out)]<class In>(In in) mutable {
      ::pushmi::submit(in, std::move(tp), std::move(out));
    };
  }
};

struct now_fn {
  auto operator()() const {
    return []<class In>(In in) PUSHMI_T_LAMBDA_REQUIRES(decltype(::pushmi::now(in)), TimeSender<In>) {
      return ::pushmi::now(in);
    };
  }
};

} // namespace detail

inline constexpr detail::set_done_fn set_done{};
inline constexpr detail::set_error_fn set_error{};
inline constexpr detail::set_value_fn set_value{};
inline constexpr detail::set_stopping_fn set_stopping{};
inline constexpr detail::set_starting_fn set_starting{};
inline constexpr detail::submit_fn submit{};
inline constexpr detail::now_fn now{};
inline constexpr detail::now_fn top{};

} // namespace extension_operators

} // namespace pushmi
