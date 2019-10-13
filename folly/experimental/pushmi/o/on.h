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

namespace folly {
namespace pushmi {

namespace detail {

struct on_fn {
 private:
  template <class Exec, class Out>
  struct down_shared : std::enable_shared_from_this<down_shared<Exec, Out>> {
    Exec exec_;
    Out out_;
    std::atomic<bool> done_;
    down_shared(Exec exec, Out out)
        : exec_(std::move(exec)), out_(std::move(out)), done_(false) {}
    Exec& exec() {
      return exec_;
    }
    Out& out() {
      return out_;
    }
    std::atomic<bool>& done() {
      return done_;
    }
  };

  template <class DownShared, class Up>
  struct up_shared : std::enable_shared_from_this<up_shared<DownShared, Up>> {
    DownShared shared_;
    Up up_;
    up_shared(DownShared shared, Up up)
        : shared_(std::move(shared)), up_(std::move(up)) {}
    auto& exec() {
      return shared_->exec();
    }
    auto& out() {
      return shared_->out();
    }
    std::atomic<bool>& done() {
      return shared_->done();
    }
  };

  template <class Shared, class Dest, class CPO, class... AN>
  struct continuation_impl {
    using receiver_category = receiver_tag;
    const CPO& fn_;
    std::tuple<Dest, AN...> an_;
    Shared shared_;
    void value(any) {
      ::folly::pushmi::apply(fn_, std::move(an_));
    }
    template <class E>
    void error(E e) noexcept {
      set_error(shared_->out(), e);
    }
    void done() {}
  };
  template <class Shared, class CPO, class... AN>
  static void down(Shared& shared, const CPO& fn, AN&&... an) {
    submit(
        ::folly::pushmi::schedule(shared->exec_),
        continuation_impl<
            Shared,
            decltype(shared->out_)&,
            CPO,
            std::decay_t<AN>...>{
            fn,
            std::tuple<decltype(shared->out_)&, std::decay_t<AN>...>{
                shared->out_, (AN &&) an...},
            shared});
  }
  template <class Shared, class CPO, class... AN>
  static void up(Shared& shared, const CPO& fn, AN&&... an) {
    if (shared->done().load()) {
      return;
    }
    submit(
        ::folly::pushmi::schedule(shared->exec()),
        continuation_impl<
            Shared,
            decltype(shared->up_)&,
            CPO,
            std::decay_t<AN>...>{
            fn,
            std::tuple<decltype(shared->up_)&, std::decay_t<AN>...>{
                shared->up_, (AN &&) an...},
            shared});
  }

  template <class Shared>
  struct up_impl {
    Shared shared_;
    using receiver_category = receiver_tag;
    template <class... VN>
    void value(VN&&... vn) {
      // call up.set_value from executor context
      up(shared_, set_value, (VN &&) vn...);
    }
    template <class E>
    void error(E&& e) noexcept {
      // call up.set_error from executor context
      up(shared_, set_error, (E &&) e);
    }
    void done() {
      // call up.set_done from executor context
      up(shared_, set_done);
    }
  };

  template <bool IsFlow, class Shared>
  struct down_impl {
    Shared shared_;
    using receiver_category =
        std::conditional_t<IsFlow, flow_receiver_tag, receiver_tag>;
    template <class... VN>
    void value(VN&&... vn) {
      if (shared_->done().load()) {
        return;
      }
      // call out.set_value from executor context
      down(shared_, set_value, (VN &&) vn...);
    }
    template <class E>
    void error(E&& e) noexcept {
      if (shared_->done().exchange(true)) {
        return;
      }
      // call out.set_error from executor context
      down(shared_, set_error, (E &&) e);
    }
    void done() {
      if (shared_->done().exchange(true)) {
        return;
      }
      // call out.set_done from executor context
      down(shared_, set_done);
    }
    PUSHMI_TEMPLATE(class Up)
    (requires IsFlow&& Receiver<Up>) //
        void starting(Up&& up) {
      auto shared =
          std::make_shared<up_shared<Shared, Up>>(shared_, (Up &&) up);
      auto receiver = up_impl<decltype(shared)>{shared};
      // call out.set_starting from executor context
      down(shared_, set_starting, std::move(receiver));
    }
  };

  template <class Factory, class In>
  struct submit_impl {
    Factory ef_;
    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires SenderTo<In, Out>) //
        void
        operator()(SIn&& in, Out out) {
      auto exec = ::folly::pushmi::make_strand(ef_);
      auto shared = std::make_shared<down_shared<decltype(exec), Out>>(
          std::move(exec), (Out &&) out);
      auto receiver = down_impl<FlowSender<In>, decltype(shared)>{shared};
      // call in.submit from executor context
      submit(
          ::folly::pushmi::schedule(shared->exec()),
          continuation_impl<
              decltype(shared),
              std::decay_t<In>,
              decltype(submit),
              decltype(receiver)>{
              submit,
              std::tuple<std::decay_t<In>, decltype(receiver)>{
                  (In &&) in, std::move(receiver)},
              shared});
    }
  };

  template <class Factory>
  struct adapt_impl {
    Factory ef_;
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>) //
        auto
        operator()(In&& in) & {
      return ::folly::pushmi::detail::sender_from(
          (In &&) in, submit_impl<Factory, In>{ef_});
    }
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>) //
        auto
        operator()(In&& in) && {
      return ::folly::pushmi::detail::sender_from(
          (In &&) in, submit_impl<Factory, In>{std::move(ef_)});
    }
  };

 public:
  PUSHMI_TEMPLATE(class Factory)
  (requires StrandFactory<Factory>) //
      auto
      operator()(Factory ef) const {
    return adapt_impl<Factory>{std::move(ef)};
  }
};

} // namespace detail

namespace operators {

PUSHMI_INLINE_VAR constexpr detail::on_fn on{};

} // namespace operators

} // namespace pushmi
} // namespace folly
