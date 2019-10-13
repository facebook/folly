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

#include <tuple>

#include <folly/experimental/pushmi/executor/concepts.h>
#include <folly/experimental/pushmi/executor/primitives.h>
#include <folly/experimental/pushmi/sender/concepts.h>
#include <folly/experimental/pushmi/sender/primitives.h>
#include <folly/experimental/pushmi/receiver/concepts.h>
#include <folly/experimental/pushmi/receiver/primitives.h>
#include <folly/experimental/pushmi/detail/functional.h>
#include <folly/experimental/pushmi/executor/executor.h>
#include <folly/experimental/pushmi/executor/inline.h>
#include <folly/experimental/pushmi/executor/trampoline.h>
#include <folly/experimental/pushmi/sender/flow_sender.h>
#include <folly/experimental/pushmi/receiver/flow_receiver.h>
#include <folly/experimental/pushmi/sender/flow_single_sender.h>
#include <folly/experimental/pushmi/forwards.h>
#include <folly/experimental/pushmi/sender/sender.h>
#include <folly/experimental/pushmi/piping.h>
#include <folly/experimental/pushmi/properties.h>
#include <folly/experimental/pushmi/receiver/receiver.h>
#include <folly/experimental/pushmi/sender/single_sender.h>
#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {

#if __cpp_lib_apply >= 201603 || not PUSHMI_NOT_ON_WINDOWS
using std::apply;
#else
namespace detail {
PUSHMI_TEMPLATE(class F, class Tuple, std::size_t... Is)
(requires requires( //
    ::folly::pushmi::invoke(
        std::declval<F>(),
        std::get<Is>(std::declval<Tuple>())...))) //
    constexpr decltype(auto)
        apply_impl(F&& f, Tuple&& t, std::index_sequence<Is...>)
        noexcept(noexcept(::folly::pushmi::invoke((F &&) f, std::get<Is>((Tuple &&) t)...)))
        {
  return ::folly::pushmi::invoke((F &&) f, std::get<Is>((Tuple &&) t)...);
}
template <class Tuple_, class Tuple = std::remove_reference_t<Tuple_>>
using tupidxs = std::make_index_sequence<std::tuple_size<Tuple>::value>;
} // namespace detail

PUSHMI_TEMPLATE(class F, class Tuple)
(requires requires( //
    detail::apply_impl(
        std::declval<F>(),
        std::declval<Tuple>(),
        detail::tupidxs<Tuple>{}))) //
    constexpr decltype(auto) apply(F&& f, Tuple&& t)
    noexcept(noexcept(detail::apply_impl((F &&) f, (Tuple &&) t, detail::tupidxs<Tuple>{})))
    {
  return detail::apply_impl((F &&) f, (Tuple &&) t, detail::tupidxs<Tuple>{});
}
#endif

namespace detail {

template <bool IsFlow = false>
struct make_receiver;
template <>
struct make_receiver<> : construct_deduced<receiver> {};
template <>
struct make_receiver<true> : construct_deduced<flow_receiver> {};

template <bool IsFlow>
struct receiver_from_impl {
  using MakeReceiver = make_receiver<IsFlow>;
  template <class... AN>
  using receiver_type = ::folly::pushmi::invoke_result_t<MakeReceiver&, AN...>;

  PUSHMI_TEMPLATE(class... Ts)
  (requires Invocable<MakeReceiver&, Ts...>) //
  auto operator()(std::tuple<Ts...> args) const {
    return ::folly::pushmi::apply(MakeReceiver(), std::move(args));
  }

  PUSHMI_TEMPLATE(class... Ts, class F0, class... Fns)
  (requires And<SemiMovable<F0>, SemiMovable<Fns>...>&&
    Invocable<MakeReceiver&, Ts...>&&
    Invocable<const receiver_from_impl&, receiver_type<Ts...>, F0, Fns...>) //
  auto operator()(std::tuple<Ts...> args, F0 f0, Fns... fns) const {
    return (*this)((*this)(std::move(args)), std::move(f0), std::move(fns)...);
  }

  PUSHMI_TEMPLATE(class Out, class... Fns)
  (requires not is_v<Out, std::tuple>&& And<MoveConstructible<Fns&&>...>) //
  auto operator()(Out&& out, Fns&&... fns) const {
    return MakeReceiver()((Out&&)out, (Fns&&)fns...);
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender) In>
using receiver_from_fn = receiver_from_impl<FlowSender<In>>;

template <PUSHMI_TYPE_CONSTRAINT(Sender) In, class... AN>
using receiver_type_t =
  typename receiver_from_fn<In>::template receiver_type<AN...>;

template <class SenderCategory>
struct make_sender;
template <>
struct make_sender<single_sender_tag> : construct_deduced<single_sender> {};
template <>
struct make_sender<sender_tag> : construct_deduced<sender> {};
template <>
struct make_sender<flow_single_sender_tag>
    : construct_deduced<flow_single_sender> {};
template <>
struct make_sender<flow_sender_tag>
    : construct_deduced<flow_sender> {};

PUSHMI_INLINE_VAR constexpr struct sender_from_fn {
  PUSHMI_TEMPLATE(class In, class... FN)
  (requires Sender<In>) //
  auto operator()(In in, FN&&... fn) const {
    return make_sender<sender_category_t<In>>{}(std::move(in), (FN &&) fn...);
  }
} const sender_from{};

struct set_value_fn {
 private:
  template <class... VN>
  struct impl {
    std::tuple<VN...> vn_;
    PUSHMI_TEMPLATE(class Out)
    (requires ReceiveValue<Out, VN...>) //
    void operator()(Out& out) {
      ::folly::pushmi::apply(
          ::folly::pushmi::set_value,
          std::tuple_cat(std::tuple<Out>{std::move(out)}, std::move(vn_)));
    }
  };

 public:
  template <class... VN>
  auto operator()(VN&&... vn) const {
    return impl<std::decay_t<VN>...>{std::tuple<VN&&...>{(VN &&) vn...}};
  }
};

struct set_error_fn {
 private:
  template <class E>
  struct impl {
    E e_;
    PUSHMI_TEMPLATE(class Out)
    (requires ReceiveError<Out, E>) //
    void operator()(Out& out) {
      set_error(out, std::move(e_));
    }
  };

 public:
  PUSHMI_TEMPLATE(class E)
  (requires SemiMovable<E>) //
  auto operator()(E e) const {
    return impl<E>{std::move(e)};
  }
};

struct set_done_fn {
 private:
  struct impl {
    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>) //
    void operator()(Out& out) {
      set_done(out);
    }
  };

 public:
  auto operator()() const {
    return impl{};
  }
};

struct set_starting_fn {
 private:
  template <class Up>
  struct impl {
    Up up_;
    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>) //
    void operator()(Out& out) {
      set_starting(out, std::move(up_));
    }
  };

 public:
  PUSHMI_TEMPLATE(class Up)
  (requires Receiver<Up>) //
  auto operator()(Up up) const {
    return impl<Up>{std::move(up)};
  }
};

struct get_executor_fn {
 private:
  struct impl {
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>) //
    auto operator()(In& in) const {
      return get_executor(in);
    }
  };

 public:
  auto operator()() const {
    return impl{};
  }
};

struct make_strand_fn {
 private:
  struct impl {
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>) //
    auto operator()(In& in) const {
      return make_strand(in);
    }
  };

 public:
  auto operator()() const {
    return impl{};
  }
};

struct do_submit_fn {
 private:
  template <class Out>
  struct impl {
    Out out_;
    PUSHMI_TEMPLATE(class In)
    (requires SenderTo<In, Out>) //
    void operator()(In&& in) && {
      submit((In&&)in, std::move(out_));
    }
    PUSHMI_TEMPLATE(class In)
    (requires SenderTo<In, Out>) //
    void operator()(In&& in) & {
      submit((In&&)in, out_);
    }
  };

 public:
  PUSHMI_TEMPLATE(class Out)
  (requires Receiver<Out>) //
  auto operator()(Out out) const {
    return impl<Out>{std::move(out)};
  }
};

struct do_schedule_fn {
 private:
  struct impl {
    PUSHMI_TEMPLATE(class Ex, class... VN)
    (requires Executor<Ex>) //
    auto operator()(Ex& ex, VN&&... vn) {
      return schedule(ex, (VN&&)vn...);
    }
  };

 public:
  auto operator()() const {
    return impl{};
  }
};

struct top_fn {
 private:
  struct impl {
    PUSHMI_TEMPLATE(class In)
    (requires ConstrainedExecutor<std::decay_t<In>>) //
    auto operator()(In& in) const {
      return ::folly::pushmi::top(in);
    }
  };

 public:
  auto operator()() const {
    return impl{};
  }
};

struct now_fn {
 private:
  struct impl {
    PUSHMI_TEMPLATE(class In)
    (requires TimeExecutor<std::decay_t<In>>) //
    auto operator()(In& in) const {
      return ::folly::pushmi::now(in);
    }
  };

 public:
  auto operator()() const {
    return impl{};
  }
};

} // namespace detail

namespace extension_operators {

PUSHMI_INLINE_VAR constexpr detail::set_done_fn set_done{};
PUSHMI_INLINE_VAR constexpr detail::set_error_fn set_error{};
PUSHMI_INLINE_VAR constexpr detail::set_value_fn set_value{};
PUSHMI_INLINE_VAR constexpr detail::set_starting_fn set_starting{};
PUSHMI_INLINE_VAR constexpr detail::get_executor_fn get_executor{};
PUSHMI_INLINE_VAR constexpr detail::make_strand_fn make_strand{};
PUSHMI_INLINE_VAR constexpr detail::do_submit_fn submit{};
PUSHMI_INLINE_VAR constexpr detail::do_schedule_fn schedule{};
PUSHMI_INLINE_VAR constexpr detail::now_fn now{};
PUSHMI_INLINE_VAR constexpr detail::top_fn top{};

} // namespace extension_operators

} // namespace pushmi
} // namespace folly
