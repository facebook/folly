// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <functional>
#include "../single.h"
#include "../boosters.h"
#include "extension_operators.h"
#include "../trampoline.h"

namespace pushmi {

namespace operators {

namespace detail {

inline constexpr struct make_receiver_fn {
  template <class... AN>
  deduced_type_t<none, AN...> operator()(none_tag, AN&& ...an) const {
    return none{(AN&&) an...};
  }
  template <class... AN>
  deduced_type_t<single, AN...> operator()(single_tag, AN&& ...an) const {
    return single{(AN&&) an...};
  }
} make_receiver {};

template <Sender In, class ...AN>
using receiver_type_t =
    std::invoke_result_t<make_receiver_fn, sender_category_t<In>, AN...>;

template <class In, class ... AN>
concept bool AutoSenderTo = SenderTo<In, receiver_type_t<In, AN...>>;

template <class In, class ... AN>
concept bool AutoTimeSenderTo = TimeSenderTo<In, receiver_type_t<In, AN...>>;

struct submit_fn {
private:
  // TODO - only move, move-only types..
  // if out can be copied, then submit can be called multiple
  // times..
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;
    template <AutoSenderTo<AN...> In>
    In operator()(In in) {
      auto out{::pushmi::detail::out_from<In>(std::move(args_))};
      ::pushmi::submit(in, std::move(out));
      return in;
    }
    template <AutoTimeSenderTo<AN...> In>
    In operator()(In in) {
      auto out{::pushmi::detail::out_from<In>(std::move(args_))};
      ::pushmi::submit(in, ::pushmi::now(in), std::move(out));
      return in;
    }
  };
public:
  template <class... AN>
  auto operator()(AN&&... an) const {
    return submit_fn::fn<AN...>{{(AN&&) an...}};
  }
};

struct submit_at_fn {
private:
  template <Regular TP, class... AN>
  struct fn {
    TP at_;
    std::tuple<AN...> args_;
    template <AutoTimeSenderTo<AN...> In>
    In operator()(In in) {
      auto out{::pushmi::detail::out_from<In>(std::move(args_))};
      ::pushmi::submit(in, std::move(at_), std::move(out));
      return in;
    }
  };
public:
  template <Regular TP, class... AN>
  auto operator()(TP at, AN... an) const {
    return submit_at_fn::fn<TP, AN...>{std::move(at), {(AN&&) an...}};
  }
};

struct submit_after_fn {
private:
  template <Regular D, class... AN>
  struct fn {
    D after_;
    std::tuple<AN...> args_;
    template <AutoTimeSenderTo<AN...> In>
    In operator()(In in) {
      // TODO - only move, move-only types..
      // if out can be copied, then submit can be called multiple
      // times..
      auto out{::pushmi::detail::out_from<In>(std::move(args_))};
      auto at = ::pushmi::now(in) + std::move(after_);
      ::pushmi::submit(in, std::move(at), std::move(out));
      return in;
    }
  };
public:
  template <Regular D, class... AN>
  auto operator()(D after, AN... an) const {
    return submit_after_fn::fn<D, AN...>{std::move(after), {(AN&&) an...}};
  }
};

struct blocking_submit_fn {
private:
  // TODO - only move, move-only types..
  // if out can be copied, then submit can be called multiple
  // times..
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;

    template <bool IsTimeSender, class In>
    In impl_(In in) {
      bool done = false;
      std::condition_variable signaled;
      auto out{::pushmi::detail::out_from<In>(
        std::move(args_),
        on_value{[&]<class Out, class V>(Out out, V&& v){
          if constexpr ((bool)TimeSender<std::remove_cvref_t<V>>) {
            // to keep the blocking semantics, make sure that the
            // nested submits block here to prevent a spurious
            // completion signal
            auto nest = ::pushmi::nested_trampoline();
            ::pushmi::submit(nest, ::pushmi::now(nest), std::move(out));
          } else {
            ::pushmi::set_value(out, (V&&) v);
          }
          done = true;
          signaled.notify_all();
        }},
        on_error{[&](auto out, auto e) noexcept {
          ::pushmi::set_error(out, std::move(e));
          done = true;
          signaled.notify_all();
        }},
        on_done{[&](auto out){
          ::pushmi::set_done(out);
          done = true;
          signaled.notify_all();
        }}
      )};
      if constexpr ((bool)IsTimeSender) {
        ::pushmi::submit(in, ::pushmi::now(in), std::move(out));
      } else {
        ::pushmi::submit(in, std::move(out));
      }
      std::mutex lock;
      std::unique_lock<std::mutex> guard{lock};
      signaled.wait(guard, [&](){
        return done;
      });
      return in;
    }

    template <AutoSenderTo<AN...> In>
    In operator()(In in) {
      return this->impl_<false>(std::move(in));
    }
    template <AutoTimeSenderTo<AN...> In>
    In operator()(In in) {
      return this->impl_<true>(std::move(in));
    }
  };
public:
  template <class... AN>
  auto operator()(AN... an) const {
    return blocking_submit_fn::fn<AN...>{{(AN&&) an...}};
  }
};

template <class T>
struct get_fn {
  // TODO constrain this better
  template <Sender In>
  T operator()(In in) const {
    std::optional<T> result_;
    std::exception_ptr ep_;
    auto out = single{
      on_value{[&](T t){ result_ = std::move(t); }},
      on_error{
        [&](auto e) noexcept { ep_ = std::make_exception_ptr(e); },
        [&](std::exception_ptr ep) noexcept { ep_ = ep; }}
    };
    using Out = decltype(out);
    static_assert(SenderTo<In, Out, single_tag> ||
        TimeSenderTo<In, Out, single_tag>,
        "'In' does not deliver value compatible with 'T' to 'Out'");
    blocking_submit_fn{}(std::move(out))(in);
    if (!!ep_) { std::rethrow_exception(ep_); }
    return std::move(*result_);
  }
};

} // namespace detail

inline constexpr detail::submit_fn submit{};
inline constexpr detail::submit_at_fn submit_at{};
inline constexpr detail::submit_after_fn submit_after{};
inline constexpr detail::blocking_submit_fn blocking_submit{};
template <class T>
inline constexpr detail::get_fn<T> get{};

} // namespace operators

} // namespace pushmi
