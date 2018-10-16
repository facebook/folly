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

// template <Sender In>
// struct out_from_fn {
//
// };





template<Sender In, class... AN>
auto out_from(std::tuple<AN...>&& args) {
  using SingleReceiver =
      decltype(pushmi::sfinae_from_tuple<single>(std::move(args)));
  using NoneReceiver =
      decltype(pushmi::sfinae_from_tuple<none>(std::move(args)));
  if constexpr ((bool)TimeSenderTo<In, SingleReceiver, single_tag> ||
      SenderTo<In, SingleReceiver, single_tag>) {
    return pushmi::from_tuple<single>(std::move(args));
  } else if constexpr ((bool)TimeSenderTo<In, NoneReceiver, none_tag> ||
      SenderTo<In, NoneReceiver, none_tag>) {
    return pushmi::from_tuple<none>(std::move(args));
  }
}

template<Sender In, class... AN, class... DVFN, class... DEFN, class... DDFN>
auto out_from(
    std::tuple<AN...>&& args,
    on_value<DVFN...> vf,
    on_error<DEFN...> ef,
    on_done<DDFN...> df) {
  auto out = out_from<In>(std::move(args));
  if constexpr (::pushmi::detail::is_v<decltype(out), single>) {
    return single{std::move(out), std::move(vf), std::move(ef), std::move(df)};
  } else if constexpr (::pushmi::detail::is_v<decltype(out), none>) {
    return none{std::move(out), std::move(ef), std::move(df)};
  }
}

template<Sender In, Receiver Out, class... DVFN, class... DEFN, class... DDFN>
auto out_from(
    Out&& out,
    on_value<DVFN...> vf,
    on_error<DEFN...> ef,
    on_done<DDFN...> df) {
  using SingleReceiver =
      decltype(pushmi::sfinae_from_tuple<single>(std::tuple{std::move(out)}));
  using NoneReceiver =
      decltype(pushmi::sfinae_from_tuple<none>(std::tuple{std::move(out)}));
  if constexpr ((bool)TimeSenderTo<In, SingleReceiver, single_tag> ||
      SenderTo<In, SingleReceiver, single_tag>) {
    return single{std::move(out), std::move(vf), std::move(ef), std::move(df)};
  } else if constexpr ((bool)TimeSenderTo<In, NoneReceiver, none_tag> ||
      SenderTo<In, NoneReceiver, none_tag>) {
    return none{std::move(out), std::move(ef), std::move(df)};
  }
}

template<Sender In, Receiver Out, class... DEFN, class... DDFN>
auto out_from(Out&& out, on_error<DEFN...> ef, on_done<DDFN...> df) {
  using SingleReceiver =
      decltype(pushmi::sfinae_from_tuple<single>(std::tuple{std::move(out)}));
  using NoneReceiver =
      decltype(pushmi::sfinae_from_tuple<none>(std::tuple{std::move(out)}));
  if constexpr ((bool)TimeSenderTo<In, SingleReceiver, single_tag> ||
      SenderTo<In, SingleReceiver, single_tag>) {
    return single{std::move(out), std::move(ef), std::move(df)};
  } else if constexpr ((bool)TimeSenderTo<In, NoneReceiver, none_tag> ||
      SenderTo<In, NoneReceiver, none_tag>) {
    return none{std::move(out), std::move(ef), std::move(df)};
  }
}

template<Sender In, Receiver Out, class... DVFN>
auto out_from(Out&& out, on_value<DVFN...> vf) {
  using SingleReceiver =
      decltype(pushmi::sfinae_from_tuple<single>(std::tuple{std::move(out)}));
  using NoneReceiver =
      decltype(pushmi::sfinae_from_tuple<none>(std::tuple{std::move(out)}));
  if constexpr ((bool)TimeSenderTo<In, SingleReceiver, single_tag> ||
      SenderTo<In, SingleReceiver, single_tag>) {
    return single{std::move(out), std::move(vf)};
  } else if constexpr ((bool)TimeSenderTo<In, NoneReceiver, none_tag> ||
      SenderTo<In, NoneReceiver, none_tag>) {
    return none{std::move(out)};
  }
}

template<Sender In, Receiver Out>
auto out_from(Out&& out) {
  using SingleReceiver =
      decltype(pushmi::sfinae_from_tuple<single>(std::tuple{std::move(out)}));
  using NoneReceiver =
      decltype(pushmi::sfinae_from_tuple<none>(std::tuple{std::move(out)}));
  if constexpr ((bool)TimeSenderTo<In, SingleReceiver, single_tag> ||
      SenderTo<In, SingleReceiver, single_tag>) {
    return single{std::move(out)};
  } else if constexpr ((bool)TimeSenderTo<In, NoneReceiver, none_tag> ||
      SenderTo<In, NoneReceiver, none_tag>) {
    return none{std::move(out)};
  }
}


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

template<Sender In, class Out, class... FN>
auto deferred_from(FN&&... fn) {
  if constexpr ((bool)TimeSenderTo<In, Out, single_tag>) {
    return time_single_deferred{(FN&&) fn...};
  } else if constexpr ((bool)SenderTo<In, Out, single_tag>) {
    return single_deferred{(FN&&) fn...};
  } else if constexpr ((bool)SenderTo<In, Out>) {
    return deferred{(FN&&) fn...};
  }
}

template<Sender In, class Out, class... FN>
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
    class Out,
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

struct do_submit_fn {
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

struct get_now_fn {
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
inline constexpr detail::do_submit_fn submit{};
inline constexpr detail::get_now_fn now{};
inline constexpr detail::get_now_fn top{};

} // namespace extension_operators

} // namespace pushmi
