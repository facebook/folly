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

#include <folly/experimental/pushmi/executor/executor.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/piping.h>
#include <folly/experimental/pushmi/receiver/flow_receiver.h>
#include <folly/experimental/pushmi/receiver/receiver.h>

#include <folly/synchronization/detail/Sleeper.h>

namespace folly {
namespace pushmi {

namespace detail {

// TODO promote flow_lifetime to a general facility

//
// signals that occur in order
//
// an earlier signal should never
// happen after a later signal
//
enum class flow_progress {
  invalid = 0,
  construct,
  start,
  stop,
  abort,
  done,
  error
};

template <class E = std::exception_ptr, class V = std::ptrdiff_t>
using flow_lifetime_request_channel_t =
    any_receiver<std::exception_ptr, std::ptrdiff_t>;

struct flow_lifetime_debug_on {
  template<class FlowLifetime>
  static void dump(FlowLifetime* that, const char* message) noexcept {
    static const char* progress[] = {
        "invalid", "construct", "start", "stop", "abort", "done", "error"};
    printf(
        "flow_lifetime: %ld values pending, %s, %s - %s\n",
        that->requested_,
        that->up_thread_.load() != std::thread::id{} ? "up on stack"
                                                 : "up not on stack",
        that->valid() ? progress[static_cast<int>(that->progress_)] : "invalid progress",
        message);
    fflush(stdout);
  }
};
struct flow_lifetime_debug_off {
  template<class FlowLifetime>
  static void dump(FlowLifetime* , const char* ) noexcept {
  }
};

//
// tracks the signals that occur in during the lifetime of the
// FlowSender/FlowReceiver
//
// verify that all transitions are made safely.
//
// allow the caller to wait for the up channel
// usage to exit prior to invalidating the up channel
//
// all functions other than wait_for_up_thread() are intended to be called on
// a strand that provides the synchronization necessary
//
template <class E = std::exception_ptr, class V = std::ptrdiff_t, class Debug = flow_lifetime_debug_off>
struct flow_lifetime {
  flow_progress progress_;
  std::ptrdiff_t requested_;
  flow_lifetime_request_channel_t<E, V> up_;
  std::atomic<std::thread::id> up_thread_;

  void dump(const char* message) noexcept {
    Debug::template dump<flow_lifetime<E, V, Debug>>(this, message);
  }

  flow_lifetime() : progress_(flow_progress::construct), requested_(0) {}

  bool valid() const noexcept {
    return progress_ > flow_progress::invalid &&
        progress_ <= flow_progress::error;
  }
  bool running() const noexcept {
    return progress_ == flow_progress::start;
  }
  bool stopping() const noexcept {
    return progress_ == flow_progress::stop ||
        progress_ == flow_progress::abort;
  }
  bool stopped() const noexcept {
    return progress_ >= flow_progress::done && valid();
  }
  bool up_valid() const noexcept {
    return running() || stopping();
  }

  // when up_thread has not been claimed, claim it and exit.
  // when up_thread has been claimed by a different thread, wait.
  // when up_thread has been claimed by this thread, exit to unwind.
  void wait_for_up_thread() noexcept {
    const auto empty = std::thread::id{};
    const auto current = std::this_thread::get_id();
    auto oldValue = up_thread_.load(std::memory_order_acquire);
    if (oldValue == current)
      return;

    folly::detail::Sleeper spin;
    do {
      while (oldValue != empty) {
        // spin/yield
        spin.wait();
        oldValue = up_thread_.load(std::memory_order_acquire);
      }
    } while (!up_thread_.compare_exchange_weak(
        oldValue,
        current,
        std::memory_order_release,
        std::memory_order_acquire));
  }

  template <class Available>
  std::ptrdiff_t filter_requested(Available available) const noexcept {
    return running() ? std::min(std::ptrdiff_t(available), requested_) : 0;
  }

  void start(flow_lifetime_request_channel_t<E, V>&& up) noexcept {
    if (progress_ != flow_progress::construct) {
      std::terminate();
    }
    up_ = std::move(up);
    progress_ = flow_progress::start;
  }

  void request(std::ptrdiff_t requested) noexcept {
    if (requested < 0) {
      std::terminate();
    }
    auto old_thread = std::thread::id{};
    if (up_thread_.compare_exchange_strong(
            old_thread, std::this_thread::get_id(),
            std::memory_order_acquire,
            std::memory_order_relaxed)) {
      // done and error will be waiting while we request more from up_
      if (!running()) {
        std::terminate();
      }
      requested_ += requested;
      set_value(up_, requested);
      // if done and error have not exited yet
      if (!stopped()) {
        // signal them to exit
        up_thread_.store(std::thread::id{}, std::memory_order_release);
      }
    }
  }
  void abort(E e) noexcept {
    dump("abort");
    auto old_thread = std::thread::id{};
    if (up_thread_.compare_exchange_strong(
            old_thread, std::this_thread::get_id(),
            std::memory_order_acquire,
            std::memory_order_relaxed)) {
      // done and error will be waiting while we stop up_
      if (!running()) {
        std::terminate();
      }
      progress_ = flow_progress::abort;
      set_error(up_, std::move(e));
      // if done and error have not exited yet
      if (!stopped()) {
        // signal them to exit
        up_thread_.store(std::thread::id{}, std::memory_order_release);
      }
    }
  }
  void stop() noexcept {
    dump("stop");
    auto old_thread = std::thread::id{};
    if (up_thread_.compare_exchange_strong(
            old_thread, std::this_thread::get_id(),
            std::memory_order_acquire, std::memory_order_relaxed)) {
      // done and error will be waiting while we stop up_
      if (!running()) {
        std::terminate();
      }
      progress_ = flow_progress::stop;
      set_done(up_);
      // if done and error have not exited yet
      if (!stopped()) {
        // signal them to exit
        up_thread_.store(std::thread::id{}, std::memory_order_release);
      }
    }
  }

  void value() noexcept {
    if (!running()) {
      std::terminate();
    }
    if (--requested_ < 0) {
      std::terminate();
    }
  }
  void error() noexcept {
    dump("error");
    if (!running() && !stopping()) {
      std::terminate();
    }
    progress_ = flow_progress::error;
    up_ = flow_lifetime_request_channel_t<E, V>{};
  }
  void done() noexcept {
    dump("done");
    if (!running() && !stopping()) {
      std::terminate();
    }
    progress_ = flow_progress::done;
    up_ = flow_lifetime_request_channel_t<E, V>{};
  }
};

template <class Debug, class Exec>
struct take_until_fn_base {
  Exec exec_;
  flow_lifetime<std::exception_ptr, std::ptrdiff_t, Debug> source_;
  flow_lifetime<std::exception_ptr, std::ptrdiff_t, Debug> trigger_;
  std::function<void()> cleanupFn;
  explicit take_until_fn_base(Exec ex) : exec_(std::move(ex)) {}
};
template <class Debug, class Exec, class Out>
struct take_until_fn_shared : public take_until_fn_base<Debug, Exec> {
  take_until_fn_shared(Out out, Exec exec)
      : take_until_fn_base<Debug, Exec>(std::move(exec)), out_(std::move(out)) {}
  Out out_;

  PUSHMI_TEMPLATE(class Fn)
  (requires NothrowInvocable<Fn&>&&
       std::is_void<invoke_result_t<Fn&>>::value) //
      void set_cleanup(Fn&& fn) {
    this->source_.dump("source set_cleanup");
    this->trigger_.dump("trigger set_cleanup");
    if (!this->cleanupFn &&
        !this->source_.stopped() && !this->trigger_.stopped()) {
      this->cleanupFn = (Fn &&) fn;
    }
  }
  void cleanup() noexcept {
    this->source_.dump("source cleanup");
    this->trigger_.dump("trigger cleanup");
    if (!!this->cleanupFn && this->source_.stopped() &&
        this->trigger_.stopped()) {
      this->cleanupFn();
      this->cleanupFn = nullptr;
    }
  }
};

template <class Debug, class Out, class Exec>
auto make_take_until_fn_shared(Out out, Exec ex)
    -> std::shared_ptr<take_until_fn_shared<Debug, Exec, Out>> {
  return std::make_shared<take_until_fn_shared<Debug, Exec, Out>>(
      std::move(out), std::move(ex));
}

struct take_until_debug_on : flow_lifetime_debug_on {};
struct take_until_debug_off : flow_lifetime_debug_off {};

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
template<class Debug>
struct take_until_fn {
 private:

  // TODO: promote execute() to an algorithm
  template <class Fn>
  struct execute_receiver {
    execute_receiver(Fn&& fn) : fn_((Fn &&) fn), value_(false) {}
    std::decay_t<Fn> fn_;
    bool value_;
    using receiver_category = receiver_tag;

    void value(any) {
      value_ = true;
      fn_();
    }
    void error(any) noexcept {
      std::terminate();
    }
    void done() {
      if (!value_) {
        std::terminate();
      }
    }
  };

  template <class Exec, class Fn>
  static void execute(Exec& ex, Fn&& fn) {
    ::folly::pushmi::submit(
        ::folly::pushmi::schedule(ex), execute_receiver<Fn>{(Fn &&) fn});
  }

  template <class Shared>
  struct up_receiver_impl {
    Shared s_;

    using receiver_category = receiver_tag;

    /// emit request to source and trigger
    template <class V>
    void value(V&& v) const {
      s_->source_.dump("source consumer request");
      s_->trigger_.dump("trigger consumer request");
      execute(s_->exec_, [s = s_, v = (V &&) v]() {
        if (s->trigger_.running() && s->trigger_.filter_requested(1) == 0) {
          s->trigger_.request(1);
        }
        if (s->source_.running()) {
          s->source_.request(v);
        }
      });
    }

    /// emit done to consumer after source and trigger complete
    void error(any) noexcept {
      s_->source_.dump("source consumer abort");
      s_->trigger_.dump("trigger consumer abort");
      execute(s_->exec_, [s = s_]() {
        s->set_cleanup([s]() noexcept { set_done(s->out_); });
        if (s->source_.running()) {
          s->source_.stop();
        }
        if (s->trigger_.running()) {
          s->trigger_.stop();
        }
      });
    }

    /// emit done to consumer after source and trigger complete
    void done() {
      s_->source_.dump("source consumer stop");
      s_->trigger_.dump("trigger consumer stop");
      execute(s_->exec_, [s = s_]() {
        s->set_cleanup([s]() noexcept { set_done(s->out_); });
        if (s->source_.running()) {
          s->source_.stop();
        }
        if (s->trigger_.running()) {
          s->trigger_.stop();
        }
      });
    }
  };

  template <class Shared>
  struct trigger_receiver_impl {
    Shared s_;

    using receiver_category = flow_receiver_tag;

    /// stop source
    void value(any) const {
      s_->source_.dump("source trigger value");
      s_->trigger_.dump("trigger trigger value");
      execute(s_->exec_, [s = s_]() {
        if (s->trigger_.running()) {
          if (s->source_.running()) {
            s->source_.stop();
          }
          s->trigger_.value();
          s->trigger_.stop();
        }
      });
    }

    /// emit done to consumer after source completes
    void error(any) noexcept {
      s_->source_.dump("source trigger error");
      s_->trigger_.dump("trigger trigger error");

      s_->trigger_.wait_for_up_thread();
      execute(s_->exec_, [s = s_]() {
        s->set_cleanup([s]() noexcept { set_done(s->out_); });
        if (s->source_.running()) {
          s->source_.stop();
        }
        s->trigger_.done();
        s->source_.dump("source trigger error task");
        s->trigger_.dump("trigger trigger error task");
        s->cleanup();
      });
    }

    /// emit done to consumer after source completes
    void done() {
      s_->source_.dump("source trigger done");
      s_->trigger_.dump("trigger trigger done");

      s_->trigger_.wait_for_up_thread();

      execute(s_->exec_, [s = s_]() {
        s->set_cleanup([s]() noexcept { set_done(s->out_); });
        if (s->source_.running()) {
          s->source_.stop();
        }
        s->trigger_.done();
        s->source_.dump("source trigger done task");
        s->trigger_.dump("trigger trigger done task");

        s->cleanup();
      });
    }

    /// source and trigger are started now tell the consumer to start
    template <class Up>
    void starting(Up up) {
      s_->source_.dump("source trigger starting");
      s_->trigger_.dump("trigger trigger starting");
      execute(s_->exec_, [s = s_, up = std::move(up)]() {
        s->trigger_.start(flow_lifetime_request_channel_t<>{std::move(up)});

        // set back channel for consumer
        set_starting(s->out_, up_receiver_impl<Shared>{s});
      });
    }
  };

  template <class Shared, class Trigger>
  struct source_receiver_impl {
    Shared s_;
    Trigger t_;

    using receiver_category = flow_receiver_tag;

    /// emit value to consumer
    template <class V>
    void value(V&& v) {
      s_->source_.dump("source source value");
      s_->trigger_.dump("trigger source value");
      execute(s_->exec_, [s = s_, v = (V &&) v]() mutable {
        if (s->source_.running()) {
          s->source_.value();
          set_value(s->out_, std::move(v));
        }
      });
    }

    /// emit error to consumer after trigger completes
    template <class E>
    void error(E e) noexcept {
      s_->source_.dump("source source error");
      s_->trigger_.dump("trigger source error");
      s_->source_.wait_for_up_thread();
      execute(s_->exec_, [s = s_, e = std::move(e)]() mutable {
        s->set_cleanup([ s, e = std::move(e) ]() mutable noexcept {
          set_error(s->out_, std::move(e));
        });
        if (s->trigger_.running()) {
          s->trigger_.stop();
        }
        s->source_.error();
        s->source_.dump("source source error task");
        s->trigger_.dump("trigger source error task");
        s->cleanup();
      });
    }

    /// emit done to consumer after trigger completes
    void done() {
      s_->source_.dump("source source done");
      s_->trigger_.dump("trigger source done");

      s_->source_.wait_for_up_thread();

      execute(s_->exec_, [s = s_]() {
        s->set_cleanup([s]() noexcept { set_done(s->out_); });
        if (s->trigger_.running()) {
          s->trigger_.stop();
        }
        s->source_.done();
        s->source_.dump("source source done task");
        s->trigger_.dump("trigger source done task");

        s->cleanup();
      });
    }

    /// source has started now submit trigger
    PUSHMI_TEMPLATE(class Up)
    (requires Receiver<Up>&&
         FlowSenderTo<Trigger, trigger_receiver_impl<Shared>>) //
        void starting(Up&& up) {
      s_->source_.dump("source source starting");
      s_->trigger_.dump("trigger source starting");
      execute(
          s_->exec_, [s = s_, t = std::move(t_), up = std::move(up)]() mutable {
            s->source_.start(flow_lifetime_request_channel_t<>{std::move(up)});

            // start trigger
            ::folly::pushmi::submit(
                std::move(t), trigger_receiver_impl<Shared>{s});
          });
    }
  };

  /// submit creates a strand to use for signal coordination and submits the
  /// source
  template <class StrandF, class Trigger, class In>
  struct submit_impl {
    StrandF sf_;
    Trigger t_;

    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires Receiver<Out>&& FlowSenderTo<
        In,
        source_receiver_impl<
            std::shared_ptr<take_until_fn_shared<Debug, strand_t<StrandF>, Out>>,
            Trigger>>) //
        void
        operator()(SIn&& in, Out out) & {
      auto exec = ::folly::pushmi::make_strand(sf_);

      auto s = make_take_until_fn_shared<Debug>(std::move(out), std::move(exec));

      s->source_.dump("source submit");
      s->trigger_.dump("trigger submit");

      auto to = source_receiver_impl<decltype(s), Trigger>{std::move(s), t_};

      // start source
      ::folly::pushmi::submit((In &&) in, std::move(to));
    }
    PUSHMI_TEMPLATE_DEBUG(class SIn, class Out)
    (requires Receiver<Out>&& FlowSenderTo<
        In,
        source_receiver_impl<
            std::shared_ptr<take_until_fn_shared<Debug, strand_t<StrandF>, Out>>,
            Trigger>>) //
        void
        operator()(SIn&& in, Out out) && {
      auto exec = ::folly::pushmi::make_strand(sf_);

      auto s = make_take_until_fn_shared<Debug>(std::move(out), std::move(exec));

      s->source_.dump("source submit");
      s->trigger_.dump("trigger submit");

      auto to = source_receiver_impl<decltype(s), Trigger>{std::move(s),
                                                           std::move(t_)};

      // start source
      ::folly::pushmi::submit((In &&) in, std::move(to));
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
    PUSHMI_TEMPLATE_DEBUG(class In)
    (requires FlowSender<In>) //
        auto
        operator()(In&& in) && {
      return ::folly::pushmi::detail::sender_from(
          (In &&) in,
          submit_impl<StrandF, Trigger, In&&>{std::move(sf_), std::move(t_)});
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
PUSHMI_INLINE_VAR constexpr detail::take_until_fn<detail::take_until_debug_off> take_until{};
PUSHMI_INLINE_VAR constexpr detail::take_until_fn<detail::take_until_debug_on> debug_take_until{};
} // namespace operators

} // namespace pushmi
} // namespace folly
