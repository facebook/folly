// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <functional>
#include "../time_single_sender.h"
#include "../boosters.h"
#include "extension_operators.h"
#include "../trampoline.h"
#include "../detail/opt.h"
#include "../detail/if_constexpr.h"

namespace pushmi {
namespace detail {
namespace submit_detail {
template <PUSHMI_TYPE_CONSTRAINT(Sender) In, class ...AN>
using receiver_type_t =
    pushmi::invoke_result_t<
        pushmi::detail::make_receiver<
          property_set_index_t<properties_t<In>, is_silent<>>,
          property_query_v<properties_t<In>, is_flow<>>>,
        AN...>;

PUSHMI_CONCEPT_DEF(
  template (class In, class ... AN)
  (concept AutoSenderTo)(In, AN...),
    SenderTo<In, receiver_type_t<In, AN...>>
);
PUSHMI_CONCEPT_DEF(
  template (class In, class ... AN)
  (concept AutoConstrainedSenderTo)(In, AN...),
    ConstrainedSenderTo<In, receiver_type_t<In, AN...>> && not Time<In>
);
PUSHMI_CONCEPT_DEF(
  template (class In, class ... AN)
  (concept AutoTimeSenderTo)(In, AN...),
    TimeSenderTo<In, receiver_type_t<In, AN...>>
);
} // namespace submit_detail

struct submit_fn {
private:
  // TODO - only move, move-only types..
  // if out can be copied, then submit can be called multiple
  // times..
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoSenderTo<In, AN...>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      ::pushmi::submit(in, std::move(out));
      return in;
    }
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoConstrainedSenderTo<In, AN...>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      ::pushmi::submit(in, ::pushmi::top(in), std::move(out));
      return in;
    }
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoTimeSenderTo<In, AN...>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
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
  template <class TP, class...AN>
  struct fn {
    TP at_;
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoTimeSenderTo<In, AN...>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      ::pushmi::submit(in, std::move(at_), std::move(out));
      return in;
    }
  };
public:
  PUSHMI_TEMPLATE(class TP, class...AN)
    (requires Regular<TP>)
  auto operator()(TP at, AN... an) const {
    return submit_at_fn::fn<TP, AN...>{std::move(at), {(AN&&) an...}};
  }
};

struct submit_after_fn {
private:
  template <class D, class... AN>
  struct fn {
    D after_;
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoTimeSenderTo<In, AN...>)
    In operator()(In in) {
      // TODO - only move, move-only types..
      // if out can be copied, then submit can be called multiple
      // times..
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      auto at = ::pushmi::now(in) + std::move(after_);
      ::pushmi::submit(in, std::move(at), std::move(out));
      return in;
    }
  };
public:
  PUSHMI_TEMPLATE(class D, class...AN)
    (requires Regular<D>)
  auto operator()(D after, AN... an) const {
    return submit_after_fn::fn<D, AN...>{std::move(after), {(AN&&) an...}};
  }
};

struct blocking_submit_fn {
private:
  struct lock_state {
    bool done{false};
    std::atomic<int> nested{0};
    std::mutex lock;
    std::condition_variable signaled;
  };
  template<class Out>
  struct nested_receiver_impl;
  PUSHMI_TEMPLATE (class Exec)
    (requires Sender<Exec> && Executor<Exec>)
  struct nested_executor_impl {
    nested_executor_impl(lock_state* state, Exec ex) :
      state_(state),
      ex_(std::move(ex)) {}
    lock_state* state_;
    Exec ex_;

    using properties = properties_t<Exec>;

    auto executor() { return ::pushmi::executor(ex_); }

    PUSHMI_TEMPLATE (class CV, class Out)
      (requires Receiver<Out> && Constrained<Exec>)
    auto top() { return ::pushmi::top(ex_); }

    PUSHMI_TEMPLATE (class CV, class Out)
      (requires Receiver<Out> && Constrained<Exec>)
    void submit(CV cv, Out out) {
      ++state_->nested;
      ::pushmi::submit(ex_, cv, nested_receiver_impl<Out>{state_, std::move(out)});
    }

    PUSHMI_TEMPLATE (class Out)
      (requires Receiver<Out> && not Constrained<Exec>)
    void submit(Out out) {
      ++state_->nested;
      ::pushmi::submit(ex_, nested_receiver_impl<Out>{state_, std::move(out)});
    }
  };
  template<class Out>
  struct nested_receiver_impl {
    nested_receiver_impl(lock_state* state, Out out) :
      state_(state),
      out_(std::move(out)) {}
    lock_state* state_;
    Out out_;

    using properties = properties_t<Out>;

    template<class V>
    void value(V&& v) {
      std::exception_ptr e;
      try{
        using executor_t = remove_cvref_t<V>;
        auto n = nested_executor_impl<executor_t>{state_, (V&&) v};
        ::pushmi::set_value(out_, any_executor_ref<>{n});
      }
      catch(...) {e = std::current_exception();}
      if(--state_->nested == 0) {
        state_->signaled.notify_all();
      }
      if (e) {std::rethrow_exception(e);}
    }
    template<class E>
    void error(E&& e) noexcept {
      ::pushmi::set_error(out_, (E&&) e);
      if(--state_->nested == 0) {
        state_->signaled.notify_all();
      }
    }
    void done() {
      std::exception_ptr e;
      try{
        ::pushmi::set_done(out_);
      }
      catch(...) {e = std::current_exception();}
      if(--state_->nested == 0) {
        state_->signaled.notify_all();
      }
      if (e) {std::rethrow_exception(e);}
    }
  };
  struct nested_executor_impl_fn {
    PUSHMI_TEMPLATE (class Exec)
      (requires Executor<Exec>)
    auto operator()(lock_state* state, Exec ex) const {
      return nested_executor_impl<Exec>{state, std::move(ex)};
    }
  };
  struct on_value_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE (class Out, class Value)
      (requires Receiver<Out, is_single<>>)
    void operator()(Out out, Value&& v) const {
      using V = remove_cvref_t<Value>;
      ++state_->nested;
      PUSHMI_IF_CONSTEXPR( ((bool)Executor<V>) (
        id(::pushmi::set_value)(out, id(nested_executor_impl_fn{})(state_, id((Value&&) v)));
      ) else (
        id(::pushmi::set_value)(out, id((Value&&) v));
      ))
      std::unique_lock<std::mutex> guard{state_->lock};
      state_->done = true;
      if (--state_->nested == 0){
        state_->signaled.notify_all();
      }
    }
  };
  struct on_next_impl {
    PUSHMI_TEMPLATE (class Out, class Value)
      (requires Receiver<Out, is_many<>>)
    void operator()(Out out, Value&& v) const {
      using V = remove_cvref_t<Value>;
      ::pushmi::set_next(out, (Value&&) v);
    }
  };
  struct on_error_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE(class Out, class E)
      (requires NoneReceiver<Out, E>)
    void operator()(Out out, E e) const noexcept {
      ::pushmi::set_error(out, std::move(e));
      std::unique_lock<std::mutex> guard{state_->lock};
      state_->done = true;
      state_->signaled.notify_all();
    }
  };
  struct on_done_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void operator()(Out out) const {
      ::pushmi::set_done(out);
      std::unique_lock<std::mutex> guard{state_->lock};
      state_->done = true;
      state_->signaled.notify_all();
    }
  };
  template <bool IsConstrainedSender, bool IsTimeSender, class In>
  struct submit_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void operator()(In& in, Out out) const {
      PUSHMI_IF_CONSTEXPR( (IsTimeSender) (
        id(::pushmi::submit)(in, id(::pushmi::now)(in), std::move(out));
      ) else (
        PUSHMI_IF_CONSTEXPR( (IsConstrainedSender) (
          id(::pushmi::submit)(in, id(::pushmi::top)(in), std::move(out));
        ) else (
          id(::pushmi::submit)(in, std::move(out));
        ))
      ))
    }
  };
  // TODO - only move, move-only types..
  // if out can be copied, then submit can be called multiple
  // times..
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;

    template <bool IsConstrainedSender, bool IsTimeSender, class In>
    In impl_(In in) {
      lock_state state{};

      auto submit = submit_impl<IsConstrainedSender, IsTimeSender, In>{};
      PUSHMI_IF_CONSTEXPR( ((bool)Many<In>) (
        auto out{::pushmi::detail::receiver_from_fn<In>()(
          std::move(args_),
          on_next(on_next_impl{}),
          on_error(on_error_impl{&state}),
          on_done(on_done_impl{&state})
        )};
        submit(in, std::move(out));
      ) else (
        auto out{::pushmi::detail::receiver_from_fn<In>()(
          std::move(args_),
          on_value(on_value_impl{&state}),
          on_error(on_error_impl{&state}),
          on_done(on_done_impl{&state})
        )};
        submit(in, std::move(out));
      ))

      std::unique_lock<std::mutex> guard{state.lock};
      state.signaled.wait(guard, [&]{
        return state.done && state.nested.load() == 0;
      });
      return in;
    }

    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoSenderTo<In, AN...>)
    In operator()(In in) {
      return this->impl_<false, false>(std::move(in));
    }
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoConstrainedSenderTo<In, AN...>)
    In operator()(In in) {
      return this->impl_<true, false>(std::move(in));
    }
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoTimeSenderTo<In, AN...>)
    In operator()(In in) {
      return this->impl_<true, true>(std::move(in));
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
private:
  struct on_value_impl {
    pushmi::detail::opt<T>* result_;
    void operator()(T t) const { *result_ = std::move(t); }
  };
  struct on_error_impl {
    std::exception_ptr* ep_;
    template <class E>
    void operator()(E e) const noexcept { *ep_ = std::make_exception_ptr(e); }
    void operator()(std::exception_ptr ep) const noexcept { *ep_ = ep; }
  };
public:
  // TODO constrain this better
  PUSHMI_TEMPLATE (class In)
    (requires Sender<In>)
  T operator()(In in) const {
    pushmi::detail::opt<T> result_;
    std::exception_ptr ep_;
    auto out = make_single(
      on_value(on_value_impl{&result_}),
      on_error(on_error_impl{&ep_})
    );
    using Out = decltype(out);
    static_assert(SenderTo<In, Out, is_single<>> ||
        TimeSenderTo<In, Out, is_single<>>,
        "'In' does not deliver value compatible with 'T' to 'Out'");
    blocking_submit_fn{}(std::move(out))(in);
    if (!!ep_) { std::rethrow_exception(ep_); }
    return std::move(*result_);
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::submit_fn submit{};
PUSHMI_INLINE_VAR constexpr detail::submit_at_fn submit_at{};
PUSHMI_INLINE_VAR constexpr detail::submit_after_fn submit_after{};
PUSHMI_INLINE_VAR constexpr detail::blocking_submit_fn blocking_submit{};
template <class T>
PUSHMI_INLINE_VAR constexpr detail::get_fn<T> get{};
} // namespace operators

} // namespace pushmi
