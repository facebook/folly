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

#include <folly/experimental/pushmi/executor/concepts.h>
#include <folly/experimental/pushmi/executor/primitives.h>
#include <folly/experimental/pushmi/sender/concepts.h>
#include <folly/experimental/pushmi/sender/primitives.h>

namespace folly {
namespace pushmi {
namespace detail {

struct interval_repeat_fn {
 private:
  template <class Out, class In, class Exec, class At, class Dur, class NextFn>
  struct tick_fn {
   private:
    Out out_;
    In in_;
    Exec exec_;
    At at_;
    Dur interval_;
    NextFn next_;

    struct next_fn {
     private:
      Out out_;
      In in_;
      Exec exec_;
      At last_;
      Dur interval_;
      NextFn next_;
      bool stopped{true};

     public:
      using receiver_category = receiver_tag;

      next_fn() = default;
      next_fn(Out out, In in, Exec exec, At last, Dur interval, NextFn next)
          : out_(out),
            in_(in),
            exec_(exec),
            last_(last),
            interval_(interval),
            next_(next) {}
      template <class... Tn>
      void value(Tn&&... tn) {
        stopped = false;
        set_value(out_, (Tn &&) tn...);

        auto next_tick = last_ + interval_;
        folly::pushmi::submit(
            schedule(exec_, next_(exec_, last_, interval_, next_tick)),
            tick_fn{std::move(out_),
                    std::move(in_),
                    exec_,
                    next_tick,
                    interval_,
                    next_});
      }
      template <class E>
      void error(E&& e) noexcept {
        set_error(out_, e);
      }
      void done() {
        if (stopped) {
          set_done(out_);
        }
      }
    };

   public:
    using receiver_category = receiver_tag;

    tick_fn() = default;
    tick_fn(Out out, In in, Exec exec, At at, Dur interval, NextFn next)
        : out_(out),
          in_(in),
          exec_(exec),
          at_(at),
          interval_(interval),
          next_(next) {}
    template <class OExec>
    void value(OExec&&) {
      folly::pushmi::submit(
          in_,
          next_fn{
              std::move(out_), in_, std::move(exec_), at_, interval_, next_});
    }
    template <class E>
    void error(E&& e) noexcept {
      set_error(out_, (E &&) e);
    }
    void done() {}
  };

  template <class In, class Exec, class At, class Dur, class NextFn>
  struct submit_fn : sender_traits<In> {
   private:
    In in_;
    Exec exec_;
    At first_;
    Dur interval_;
    NextFn next_;

   public:
    submit_fn() = default;
    submit_fn(In in, Exec exec, At first, Dur interval, NextFn next)
        : in_(in),
          exec_(exec),
          first_(first),
          interval_(interval),
          next_(next) {}
    template <class Out>
    void submit(Out&& out) {
      auto strand = make_strand(exec_);
      folly::pushmi::submit(
          schedule(strand, next_(strand, first_, interval_, first_)),
          tick_fn<Out, In, decltype(strand), At, Dur, NextFn>{
              (Out &&) out, in_, strand, first_, interval_, next_});
    }
  };

  template <class Exec, class At, class Dur, class NextFn>
  struct adapt_fn {
   private:
    Exec exec_;
    At first_;
    Dur interval_;
    NextFn next_;

   public:
    adapt_fn() = default;
    adapt_fn(Exec exec, At first, Dur interval, NextFn next)
        : exec_(exec), first_(first), interval_(interval), next_(next) {}
    PUSHMI_TEMPLATE(class In)
    (requires SingleSender<In>) //
        auto
        operator()(In&& in) {
      return submit_fn<In, Exec, At, Dur, NextFn>{
          (In &&) in, exec_, first_, interval_, next_};
    }
  };

 public:
  PUSHMI_TEMPLATE(class Exec, class At, class Dur, class NextFn)
  (requires StrandFactory<Exec>&& TimeExecutor<strand_t<Exec>>&&
       Invocable<NextFn, strand_t<Exec>&, At&, Dur&, At&>&& Invocable<
           decltype(schedule),
           strand_t<Exec>&,
           invoke_result_t<NextFn, strand_t<Exec>&, At&, Dur&, At&>&>) //
      auto
      operator()(Exec exec, At first, Dur interval, NextFn next) const {
    return adapt_fn<Exec, At, Dur, NextFn>{exec, first, interval, next};
  }
  PUSHMI_TEMPLATE(class Exec, class At, class Dur)
  (requires StrandFactory<Exec>&& TimeExecutor<strand_t<Exec>>&&
       Invocable<decltype(schedule), strand_t<Exec>&, At&>) //
      auto
      operator()(Exec exec, At first, Dur interval) const {
    return (*this)(
        exec, first, interval, [](strand_t<Exec>&, At&, Dur&, At& next) {
          return next;
        });
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::interval_repeat_fn interval_repeat{};
} // namespace operators

} // namespace pushmi
} // namespace folly
