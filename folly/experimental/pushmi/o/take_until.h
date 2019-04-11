/*
 * Copyright 2018-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/experimental/pushmi/executor/executor.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/piping.h>
#include <folly/experimental/pushmi/receiver/flow_receiver.h>
#include <folly/experimental/pushmi/receiver/receiver.h>

namespace folly {
namespace pushmi {

namespace detail {

using take_until_request_channel_t =
    any_receiver<std::exception_ptr, std::ptrdiff_t>;
template <class Exec>
struct take_until_fn_base {
  Exec exec_;
  std::atomic<bool> done_;
  take_until_request_channel_t up_trigger_;
  take_until_request_channel_t up_source_;
  explicit take_until_fn_base(Exec ex) : exec_(std::move(ex)), done_(false) {}
};
template <class Exec, class Out>
struct take_until_fn_shared : public take_until_fn_base<Exec> {
  take_until_fn_shared(Out out, Exec exec)
      : take_until_fn_base<Exec>(std::move(exec)), out_(std::move(out)) {}
  Out out_;
};

template <class Out, class Exec>
auto make_take_until_fn_shared(Out out, Exec ex)
    -> std::shared_ptr<take_until_fn_shared<Exec, Out>> {
  return std::make_shared<take_until_fn_shared<Exec, Out>>(
      std::move(out), std::move(ex));
}

/// The implementation of the take_until algorithms
///
/// This algorithm will coordinate two FlowSenders source and trigger.
/// When `take_until.submit()` is called, take until will `source.submit()` and
/// `trigger.submit()`.
///
/// - signals from the source are passed through until a signal from the trigger
/// arrives.
/// - Any signal on the trigger will cancel the source and complete the
/// take_until.
/// - The done and error signals from the source will cancel the trigger and
/// pass the error or done signal onward.
///
/// This is often used to insert a cancellation point into an expression. The
/// trigger parameter can be a flow_sender that is manually fired (such as a
/// subject or stop_token) or any other valid expression that results in a
/// flow_sender.
///
struct take_until_fn {
 private:
  /// The source_receiver is submitted to the source
  template <class Out, class Exec>
  struct source_receiver {
    using shared_t = std::shared_ptr<take_until_fn_shared<Exec, Out>>;
    using properties = properties_t<Out>;
    using receiver_category = receiver_category_t<Out>;

    shared_t s_;

    template<class... VN>
    void value(VN&&... vn) {
      set_value(s_->out_, (VN &&) vn...);
    }
    template<class E>
    void error(E&& e) noexcept {
      set_error(s_->out_, (E &&) e);
    }
    void done() {
      set_done(s_->out_);
    }
    template<class Up>
    void starting(Up&& up) noexcept {
      set_starting(s_->out_, (Up &&) up);
    }
  };
  /// The trigger_receiver is submitted to the trigger
  template <class Out, class Exec>
  struct trigger_receiver : flow_receiver<> {
    using shared_t = std::shared_ptr<take_until_fn_shared<Exec, Out>>;
    shared_t s_;
    explicit trigger_receiver(shared_t s) : s_(s) {}
  };
  /// The request_receiver is sent to the consumer when starting
  template <class Out, class Exec>
  struct request_receiver : receiver<> {
    using shared_t = std::shared_ptr<take_until_fn_shared<Exec, Out>>;
    shared_t s_;
    explicit request_receiver(shared_t s) : s_(s) {}
  };
  /// passes source values to consumer
  template <class Out>
  struct on_value_impl {
    template <class V>
    struct impl {
      V v_;
      std::shared_ptr<Out> out_;
      void operator()(any) {
        set_value(out_, std::move(v_));
      }
    };
    template <class Data, class V>
    void operator()(Data& data, V&& v) const {
      if (data.s_->done_.load()) {
        return;
      }
      ::folly::pushmi::submit(
          ::folly::pushmi::schedule(data.s_->exec_),
          ::folly::pushmi::make_receiver(impl<std::decay_t<V>>{
              (V &&) v, std::shared_ptr<Out>{data.s_, &(data.s_->out_)}}));
    }
  };
  /// passes error to consumer and cancels trigger
  template <class Out>
  struct on_error_impl {
    template<class E, class Shared>
    struct impl {
      E e_;
      Shared s_;
      void operator()(any) {
        // cancel source
        set_done(s_->up_source_);
        // cancel trigger
        set_done(s_->up_trigger_);

        // cleanup circular references
        s_->up_source_ = take_until_request_channel_t{};
        s_->up_trigger_ = take_until_request_channel_t{};

        // complete consumer
        set_error(s_->out_, std::move(e_));
      }
    };
    template <class Data, class E>
    void operator()(Data& data, E e) const noexcept {
      if (data.s_->done_.exchange(true)) {
        return;
      }

      ::folly::pushmi::submit(
          ::folly::pushmi::schedule(data.s_->exec_),
          ::folly::pushmi::make_receiver(
              impl<E, decltype(data.s_)>{std::move(e), data.s_}));
    }
  };
  /// passes done to consumer and cancels trigger
  template <class Out>
  struct on_done_impl {
    template<class Shared>
    struct impl {
      Shared s_;
      void operator()(any) {
        // cancel source
        set_done(s_->up_source_);
        // cancel trigger
        set_done(s_->up_trigger_);

        // cleanup circular references
        s_->up_source_ = take_until_request_channel_t{};
        s_->up_trigger_ = take_until_request_channel_t{};

        // complete consumer
        set_done(s_->out_);
      }
    };
    template <class Data>
    void operator()(Data& data) const {
      if (data.s_->done_.exchange(true)) {
        return;
      }

      ::folly::pushmi::submit(
          ::folly::pushmi::schedule(data.s_->exec_),
          ::folly::pushmi::make_receiver(impl<decltype(data.s_)>{data.s_}));
    }
  };
  /// passes flow requests to source
  struct on_requested_impl {
    struct impl {
      std::ptrdiff_t requested_;
      std::shared_ptr<take_until_request_channel_t> up_source_;
      void operator()(any) {
        // pass requested to source
        set_value(up_source_, requested_);
      }
    };
    template <class Data, class V>
    void operator()(Data& data, V&& v) const {
      if (data.s_->done_.load()) {
        return;
      }
      ::folly::pushmi::submit(
          ::folly::pushmi::schedule(data.s_->exec_),
          ::folly::pushmi::make_receiver(
              impl{(V &&) v,
                   std::shared_ptr<take_until_request_channel_t>{
                       data.s_, &(data.s_->up_source_)}}));
    }
  };
  /// reused for all the trigger signals. cancels the source and sends done to
  /// the consumer
  template <class Out>
  struct on_trigger_impl {
    template<class Shared>
    struct impl {
      Shared s_;
      void operator()(any) {
        // cancel source
        set_done(s_->up_source_);
        // cancel trigger
        set_done(s_->up_trigger_);

        // cleanup circular references
        s_->up_source_ = take_until_request_channel_t{};
        s_->up_trigger_ = take_until_request_channel_t{};

        // complete consumer
        set_done(s_->out_);
      }
    };

    template <class Data, class... AN>
    void operator()(Data& data, AN&&...) const noexcept {
      if (data.s_->done_.exchange(true)) {
        return;
      }

      // tell consumer that the end is nigh
      ::folly::pushmi::submit(
          ::folly::pushmi::schedule(data.s_->exec_),
          ::folly::pushmi::make_receiver(impl<decltype(data.s_)>{data.s_}));
    }
  };
  /// both source and trigger are started, ask the trigger for a value and
  /// give the consumer a back channel for flow control.
  template <class Out>
  struct on_starting_trigger_impl {
    template<class Shared>
    struct impl {
      Shared s_;
      void operator()(any) {

        // set back channel for consumer
        set_starting(
            s_->out_,
            ::folly::pushmi::make_receiver(
                request_receiver<Out, decltype(s_->exec_)>{s_},
                on_requested_impl{},
                on_trigger_impl<Out>{},
                on_trigger_impl<Out>{}));

        if (!s_->done_.load()) {
          // ask for trigger
          set_value(s_->up_trigger_, 1);
        }
      }
    };
    template <class Data, class Up>
    void operator()(Data& data, Up up) const {
      data.s_->up_trigger_ = take_until_request_channel_t{std::move(up)};

      ::folly::pushmi::submit(
          ::folly::pushmi::schedule(data.s_->exec_),
          ::folly::pushmi::make_receiver(impl<decltype(data.s_)>{data.s_}));
    }
  };
  /// source has been submitted now submit trigger
  template <class Trigger, class Out>
  struct on_starting_source_impl {
    Trigger t_;

    template <class Data, class Up>
    void operator()(Data& data, Up up) {
      data.s_->up_source_ = take_until_request_channel_t{std::move(up)};

      // start trigger
      ::folly::pushmi::submit(
          t_,
          ::folly::pushmi::detail::receiver_from_fn<Trigger>()(
              trigger_receiver<Out, decltype(data.s_->exec_)>{data.s_},
              on_trigger_impl<Out>{},
              on_trigger_impl<Out>{},
              on_trigger_impl<Out>{},
              on_starting_trigger_impl<Out>{}));
    }
  };
  /// submit creates a strand to use for signal coordination and submits the
  /// source
  template <class StrandF, class Trigger, class In>
  struct submit_impl {
    StrandF sf_;
    Trigger t_;

    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires Receiver<Out>) //
        void
        operator()(SIn&& in, Out out) & {
      auto exec = ::folly::pushmi::make_strand(sf_);

      // start source
      ::folly::pushmi::submit(
          (In &&) in,
          ::folly::pushmi::detail::receiver_from_fn<In>()(
              source_receiver<Out, strand_t<StrandF>>{
                  make_take_until_fn_shared(std::move(out), std::move(exec))},
              on_value_impl<Out>{},
              on_error_impl<Out>{},
              on_done_impl<Out>{},
              on_starting_source_impl<Trigger, Out>{t_}));
    }
    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires Receiver<Out>) //
        void
        operator()(SIn&& in, Out out) && {
      auto exec = ::folly::pushmi::make_strand(sf_);

      // start source
      ::folly::pushmi::submit(
          (In &&) in,
          ::folly::pushmi::detail::receiver_from_fn<In>()(
              source_receiver<Out, strand_t<StrandF>>{
                  make_take_until_fn_shared(std::move(out), std::move(exec))},
              on_value_impl<Out>{},
              on_error_impl<Out>{},
              on_done_impl<Out>{},
              on_starting_source_impl<Trigger, Out>{std::move(t_)}));
    }
  };

  /// adapt binds the source into a new sender
  template <class StrandF, class Trigger>
  struct adapt_impl {
    StrandF sf_;
    Trigger t_;

    PUSHMI_TEMPLATE(class In)
    (requires FlowSender<In>) //
        auto
        operator()(In&& in) & {
      // copy to allow multiple calls to connect to multiple 'in'
      return ::folly::pushmi::detail::sender_from(
          (In &&) in, submit_impl<StrandF, Trigger, In&&>{sf_, t_});
    }
    PUSHMI_TEMPLATE(class In)
    (requires FlowSender<In>) //
        auto
        operator()(In&& in) && {
      return ::folly::pushmi::detail::sender_from(
          (In &&) in, submit_impl<StrandF, Trigger, In&&>{std::move(sf_), std::move(t_)});
    }
  };

 public:
  /// constructs the algorithm by storing the strand factory and Trigger sender
  PUSHMI_TEMPLATE(class StrandF, class Trigger)
  (requires StrandFactory<StrandF>&& FlowSender<Trigger>) //
      auto
      operator()(StrandF sf, Trigger t) const {
    return adapt_impl<StrandF, Trigger>{std::move(sf), std::move(t)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::take_until_fn take_until{};
} // namespace operators

} // namespace pushmi
} // namespace folly
