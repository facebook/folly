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

#include <folly/experimental/pushmi/sender/flow_sender.h>
#include <folly/experimental/pushmi/sender/sender.h>
#include <folly/experimental/pushmi/sender/properties.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/executor/trampoline.h>

namespace folly {
namespace pushmi {

PUSHMI_CONCEPT_DEF(
    template(class R) //
    concept Range, //
    requires(R&& r)( //
        implicitly_convertible_to<bool>(std::begin(r) == std::end(r))
    )
);

namespace operators {

PUSHMI_INLINE_VAR constexpr struct from_fn {
 private:
  template <class I, class S>
  struct sender_impl
  : sender_tag::with_values<typename std::iterator_traits<I>::value_type>
      ::no_error {
  private:
    I begin_;
    S end_;
  public:
    using properties = property_set<is_always_blocking<>>;
    sender_impl() = default;
    sender_impl(I begin, S end) : begin_(begin), end_(end) {}

    PUSHMI_TEMPLATE(class Out)
    (requires ReceiveValue<
        Out,
        typename std::iterator_traits<I>::value_type>) //
    void submit(Out&& out) {
      for (auto c = begin_; c != end_; ++c) {
        set_value(out, folly::as_const(*c));
      }
      set_done(out);
    }
  };

 public:
  PUSHMI_TEMPLATE(class I, class S)
  (requires DerivedFrom<
      typename std::iterator_traits<I>::iterator_category,
      std::forward_iterator_tag>) //
  auto operator()(I begin, S end) const {
    return sender_impl<I, S>{begin, end};
  }

  PUSHMI_TEMPLATE(class R)
  (requires Range<R>) //
  auto operator()(R&& range) const {
    return (*this)(std::begin(range), std::end(range));
  }
} from{};

template <class I, class S, class Out, class Exec>
struct flow_from_producer {
  flow_from_producer(I begin, S end_, Out out_, Exec exec_, bool s)
      : c(begin),
        end(end_),
        out(std::move(out_)),
        exec(std::move(exec_)),
        stop(s) {}
  I c;
  S end;
  Out out;
  Exec exec;
  std::atomic<bool> stop;
};

template <class Producer>
struct flow_from_done {
  using receiver_category = flow_receiver_tag;

  explicit flow_from_done(std::shared_ptr<Producer> p) : p_(std::move(p)) {}
  std::shared_ptr<Producer> p_;

  template <class SubExec>
  void value(SubExec) {
    set_done(p_->out);
  }

  template <class E>
  void error(E) noexcept {
    set_done(p_->out);
  }

  void done() {
  }
};

template <class Producer>
struct flow_from_up {
  using receiver_category = receiver_tag;

  explicit flow_from_up(std::shared_ptr<Producer> p) : p_(std::move(p)) {}
  std::shared_ptr<Producer> p_;

  void value(std::ptrdiff_t requested) {
    if (requested < 1) {
      return;
    }
    // submit work to exec
    ::folly::pushmi::submit(
      ::folly::pushmi::schedule(p_->exec),
      make_receiver([p = p_, requested](auto) {
        auto remaining = requested;
        // this loop is structured to work when there is
        // re-entrancy. out.value in the loop may call up.value.
        // to handle this, the state of p->c must be captured and
        // the remaining and p->c must be changed before
        // out.value is called.
        while (remaining-- > 0 && !p->stop && p->c != p->end) {
          auto i = (p->c)++;
          set_value(p->out, folly::as_const(*i));
        }
        if (p->c == p->end) {
          set_done(p->out);
        }
      }));
  }

  template <class E>
  void error(E) noexcept {
    p_->stop.store(true);
    ::folly::pushmi::submit(
        ::folly::pushmi::schedule(p_->exec),
        flow_from_done<Producer>{p_});
  }

  void done() {
    p_->stop.store(true);
    ::folly::pushmi::submit(
        ::folly::pushmi::schedule(p_->exec),
        flow_from_done<Producer>{p_});
  }
};

PUSHMI_INLINE_VAR constexpr struct flow_from_fn {
 private:
  template <class Producer>
  struct receiver_impl : pipeorigin {
    using receiver_category = receiver_tag;

    explicit receiver_impl(std::shared_ptr<Producer> p) : p_(std::move(p)) {}
    std::shared_ptr<Producer> p_;
    template<class SubExec>
    void value(SubExec) {
      // pass reference for cancellation.
      set_starting(p_->out, flow_from_up<Producer>{p_});
    }

    template <class E>
    void error(E) noexcept {
      p_->stop.store(true);
      set_done(p_->out);
    }

    void done() {
    }
  };

  template <class I, class S, class EF>
  struct sender_impl
  : flow_sender_tag::with_values<typename std::iterator_traits<I>::value_type>
      ::no_error
  , pipeorigin {
    using properties = property_set<is_always_blocking<>>;

    I begin_;
    S end_;
    EF ef_;
    sender_impl(I begin, S end, EF ef) : begin_(begin), end_(end), ef_(ef) {}
    PUSHMI_TEMPLATE(class Out)
    (requires ReceiveValue<
        Out&,
        typename std::iterator_traits<I>::value_type>) //
    void submit(Out out) & {
      auto exec = ::folly::pushmi::make_strand(ef_);
      using Exec = decltype(exec);
      using Producer = flow_from_producer<I, S, Out, Exec>;
      auto p = std::make_shared<Producer>(
          begin_, end_, std::move(out), std::move(exec), false);

      ::folly::pushmi::submit(
          ::folly::pushmi::schedule(p->exec), receiver_impl<Producer>{p});
    }
    PUSHMI_TEMPLATE(class Out)
    (requires ReceiveValue<
        Out&,
        typename std::iterator_traits<I>::value_type>) //
    void submit(Out out) && {
      auto exec = ::folly::pushmi::make_strand(ef_);
      using Exec = decltype(exec);
      using Producer = flow_from_producer<I, S, Out, Exec>;
      auto p = std::make_shared<Producer>(
          std::move(begin_), std::move(end_), std::move(out), std::move(exec), false);

      ::folly::pushmi::submit(
          ::folly::pushmi::schedule(p->exec), receiver_impl<Producer>{p});
    }
  };

 public:
  PUSHMI_TEMPLATE(class I, class S)
  (requires DerivedFrom<
      typename std::iterator_traits<I>::iterator_category,
      std::forward_iterator_tag>) //
  auto operator()(I begin, S end) const {
    return (*this)(begin, end, trampolines);
  }

  PUSHMI_TEMPLATE(class R)
  (requires Range<R>) //
  auto operator()(R&& range) const {
    return (*this)(std::begin(range), std::end(range), trampolines);
  }

  PUSHMI_TEMPLATE(class I, class S, class EF)
  (requires DerivedFrom<
      typename std::iterator_traits<I>::iterator_category,
      std::forward_iterator_tag>&& StrandFactory<EF>) //
  auto operator()(I begin, S end, EF ef) const {
    return sender_impl<I, S, EF>{begin, end, ef};
  }

  PUSHMI_TEMPLATE(class R, class EF)
  (requires Range<R>&& StrandFactory<EF>) //
  auto operator()(R&& range, EF ef) const {
    return (*this)(std::begin(range), std::end(range), ef);
  }
} flow_from{};

} // namespace operators

} // namespace pushmi
} // namespace folly
