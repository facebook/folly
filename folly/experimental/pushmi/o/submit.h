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

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>

#include <folly/experimental/pushmi/executor/concepts.h>
#include <folly/experimental/pushmi/executor/primitives.h>
#include <folly/experimental/pushmi/sender/concepts.h>
#include <folly/experimental/pushmi/sender/primitives.h>
#include <folly/experimental/pushmi/sender/properties.h>
#include <folly/experimental/pushmi/detail/opt.h>
#include <folly/experimental/pushmi/detail/functional.h>
#include <folly/experimental/pushmi/o/extension_operators.h>

namespace folly {
namespace pushmi {
namespace detail {
namespace submit_detail {

PUSHMI_CONCEPT_DEF(
  template(class In, class... AN) //
  (concept AutoSenderTo)(In, AN...), //
    SenderTo<In, receiver_type_t<std::decay_t<In>, AN...>>);
} // namespace submit_detail

struct submit_fn {
 private:
  // TODO - only move, move-only types..
  // if out can be copied, then submit can be called multiple
  // times..
  template <class... AN>
  struct fn : pipeorigin {
    explicit fn(AN&&... an) : args_((AN &&) an...) {}
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE_DEBUG(class In)
    (requires submit_detail::AutoSenderTo<In&&, AN...>) //
    void operator()(In&& in) {
      using maker_t = receiver_from_fn<std::decay_t<In>>;
      using receiver_t = invoke_result_t<maker_t, std::tuple<AN...>&&>;
      receiver_t out{maker_t{}(std::move(args_))}; (void)out;
      ::folly::pushmi::submit((In &&) in, std::move(out));
    }
  };

 public:
  template <class... AN>
  auto operator()(AN&&... an) const {
    return submit_fn::fn<AN...>{(AN &&) an...};
  }
};

struct blocking_submit_fn {
 private:
  struct lock_state {
    std::atomic<bool> notified{false}; // ensures that state survives until
                                       // the stack is unwound
    std::atomic<bool> done{false}; // ensures that blocking completes after
                                   // the source called done/error
    std::atomic<int> nested{0}; // tracks the pending nested executors
                                // and executions
    std::mutex lock;
    std::condition_variable signaled;
  };

  struct protect_stack {
    lock_state* state_;
    ~protect_stack() {
      if (--state_->nested == 0) {
        std::unique_lock<std::mutex> guard{state_->lock};
        state_->signaled.notify_all();
        state_->notified.exchange(true);
      }
    }
    protect_stack(lock_state* state) : state_(state) {
      ++state_->nested;
    }
  };

  template<class Task>
  struct nested_task_impl;

  template <class Out>
  struct nested_receiver_impl;

  PUSHMI_TEMPLATE(class Exec)
  (requires Executor<Exec>) //
  struct nested_executor_impl {
    nested_executor_impl(lock_state* state, Exec ex)
        : state_(state), ex_(std::move(ex)) {}
    lock_state* state_;
    Exec ex_;

    template <class U>
    using test_for_this = nested_executor_impl<U>;

    PUSHMI_TEMPLATE(class Ex)
    (requires Executor<Ex>&& detail::is_v<Ex, test_for_this>) //
    static auto make(lock_state*, Ex ex) {
      return ex;
    }
    PUSHMI_TEMPLATE(class Ex)
    (requires Executor<Ex> &&
     not detail::is_v<Ex, test_for_this>) //
    static auto make(lock_state* state, Ex ex) {
      return nested_executor_impl<Ex>{state, ex};
    }

    using properties = properties_t<Exec>;

    PUSHMI_TEMPLATE(class Exec_ = Exec)
    (requires ConstrainedExecutor<Exec_>) //
    auto top() {
      return ::folly::pushmi::top(ex_);
    }
    auto schedule() {
      auto protected_scope = protect_stack{state_};
      return nested_task_impl<sender_t<Exec>>{
          state_,
          ::folly::pushmi::schedule(ex_)};
    }
    PUSHMI_TEMPLATE(class Exec_ = Exec)
    (requires ConstrainedExecutor<Exec_>) //
    auto schedule(constraint_t<Exec_> at) {
      auto protected_scope = protect_stack{state_};
      return nested_task_impl<sender_t<Exec_, constraint_t<Exec_>>>{
          state_,
          ::folly::pushmi::schedule(ex_, std::move(at))};
    }
  };

  template<class Task>
  struct nested_task_impl : sender_traits<Task> {
    nested_task_impl(lock_state* state, Task t)
        : state_(state), t_(std::move(t)) {}
    lock_state* state_;
    Task t_;

    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>) //
    void submit(Out out) && {
      auto protected_scope = protect_stack{state_};
      ++state_->nested; // reversed in nested_receiver_impl ::done/::error
      ::folly::pushmi::submit(
          std::move(t_),
          nested_receiver_impl<Out>{state_, std::move(out)});
    }
  };

  template <class Out>
  struct nested_receiver_impl {
    nested_receiver_impl(lock_state* state, Out out)
        : state_(state), out_(std::move(out)) {}
    lock_state* state_;
    Out out_;

    using receiver_category = receiver_category_t<Out>;

    template <class V>
    void value(V&& v) {
      auto protected_scope = protect_stack{state_};
      using executor_t = remove_cvref_t<V>;
      auto n = nested_executor_impl<executor_t>::make(state_, (V &&) v);
      set_value(out_, n);
    }
    template <class E>
    void error(E&& e) noexcept {
      auto protected_scope = protect_stack{state_};
      set_error(out_, (E &&) e);
      --state_->nested; // reverses nested_task_impl::submit
    }
    void done() {
      auto protected_scope = protect_stack{state_};
      set_done(out_);
      --state_->nested; // reverses nested_task_impl::submit
    }
  };
  struct nested_executor_impl_fn {
    PUSHMI_TEMPLATE(class Exec)
    (requires Executor<Exec>) //
        auto
        operator()(lock_state* state, Exec ex) const {
      return nested_executor_impl<Exec>::make(state, std::move(ex));
    }
  };
  struct on_value_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE(class Out, class Value)
    (requires Executor<std::decay_t<Value>>&& ReceiveValue<
        Out,
        ::folly::pushmi::invoke_result_t<
            nested_executor_impl_fn,
            lock_state*,
            std::decay_t<Value>>>) //
        void
        operator()(Out out, Value&& v) const {
      auto protected_scope = protect_stack{state_};
      set_value(out, nested_executor_impl_fn{}(state_, (Value &&) v));
    }
    PUSHMI_TEMPLATE(class Out, class... VN)
    (requires True<>&& ReceiveValue<Out, VN...> &&
     not(sizeof...(VN) == 1 && And<Executor<std::decay_t<VN>>...>)) //
        void
        operator()(Out out, VN&&... vn) const {
      set_value(out, (VN &&) vn...);
    }
  };
  struct on_error_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE(class Out, class E)
    (requires ReceiveError<Out, E>) //
        void
        operator()(Out out, E e) const noexcept {
      auto protected_scope = protect_stack{state_};
      set_error(out, std::move(e));
      state_->done.exchange(true);
      --state_->nested; // reverses blocking_submit_fn::fn
    }
  };
  struct on_done_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>) //
        void
        operator()(Out out) const {
      auto protected_scope = protect_stack{state_};
      set_done(out);
      state_->done.exchange(true);
      --state_->nested; // reverses blocking_submit_fn::fn
    }
  };

  template <class In>
  struct receiver_impl {
    PUSHMI_TEMPLATE(class... AN)
    (requires Sender<In>) //
        auto
        operator()(lock_state* state, std::tuple<AN...> args) const {
      return ::folly::pushmi::detail::receiver_from_fn<In>()(
          std::move(args),
          on_value_impl{state},
          on_error_impl{state},
          on_done_impl{state});
    }
  };
  template <class In>
  struct submit_impl {
    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>&& SenderTo<In, Out>) //
        void
        operator()(In&& in, Out out) const {
      ::folly::pushmi::submit((In &&) in, std::move(out));
    }
  };
  // TODO - only move, move-only types..
  // if out can be copied, then submit can be called multiple
  // times..
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;

    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>&& Invocable<
         submit_impl<In>&,
         In&&,
         ::folly::pushmi::invoke_result_t<
             receiver_impl<std::decay_t<In>>&,
             lock_state*,
             std::tuple<AN...>&&>> &&
     not is_always_blocking_v<In>) //
        void
        operator()(In&& in) {
      lock_state state{};

      auto make = receiver_impl<std::decay_t<In>>{};
      auto submit = submit_impl<In>{};
      ++state.nested; // reversed by on_done_impl and on_error_impl
      submit((In &&) in, make(&state, std::move(args_)));

      std::unique_lock<std::mutex> guard{state.lock};
      state.signaled.wait(
          guard, [&] { return state.done.load() && state.nested.load() == 0; });
      while (!state.notified.load()) {}
    }
  };

 public:
  template <class... AN>
  auto operator()(AN... an) const {
    return blocking_submit_fn::fn<AN...>{std::tuple<AN...>{(AN &&) an...}};
  }
};

template <class T>
struct get_fn {
 private:
  struct on_value_impl {
    ::folly::pushmi::detail::opt<T>* result_;
    template <class... TN>
    void operator()(TN&&... tn) const {
      *result_ = T{(TN &&) tn...};
    }
  };
  struct on_error_impl {
    ::folly::pushmi::detail::opt<std::exception_ptr>* ep_;
    template <class E>
    void operator()(E e) const noexcept {
      *ep_ = std::make_exception_ptr(e);
    }
    void operator()(std::exception_ptr ep) const noexcept {
      *ep_ = ep;
    }
  };

 public:
  // TODO constrain this better
  PUSHMI_TEMPLATE(class In)
  (requires Sender<In>) //
      T
      operator()(In in) const {
    ::folly::pushmi::detail::opt<T> result_;
    ::folly::pushmi::detail::opt<std::exception_ptr> ep_;
    auto out = ::folly::pushmi::make_receiver(
        on_value_impl{&result_}, on_error_impl{&ep_});
    using Out = decltype(out);
    static_assert(
        SenderTo<In, Out>,
        "'In' does not deliver value compatible with 'T' to 'Out'");
    std::conditional_t<
      is_always_blocking_v<In>,
      submit_fn,
      blocking_submit_fn>{}(std::move(out))(std::move(in));
    if (!!ep_) {
      std::rethrow_exception(*ep_);
    }
    return std::move(*result_);
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::submit_fn submit{};
PUSHMI_INLINE_VAR constexpr detail::blocking_submit_fn blocking_submit{};
template <class T>
PUSHMI_INLINE_VAR constexpr detail::get_fn<T> get{};
} // namespace operators

} // namespace pushmi
} // namespace folly
