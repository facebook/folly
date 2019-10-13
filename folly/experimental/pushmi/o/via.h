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

template <class Exec>
struct via_fn_base {
  Exec exec_;
  bool done_;
  explicit via_fn_base(Exec exec) : exec_(std::move(exec)), done_(false) {}
  via_fn_base& via_fn_base_ref() {
    return *this;
  }
};
template <class Exec, class Out>
struct via_fn_data : via_fn_base<Exec> {
  via_fn_data(Out out, Exec exec)
      : via_fn_base<Exec>(std::move(exec))
      , out_(std::make_shared<Out>(std::move(out))) {}

  using receiver_category = receiver_category_t<Out>;

  template <class CPO, class... AN>
  struct impl {
    using receiver_category = receiver_tag;
    const CPO& fn_;
    std::tuple<Out&, AN...> an_;
    std::shared_ptr<Out> out_;
    void value(any) {
      ::folly::pushmi::apply(fn_, std::move(an_));
    }
    template<class E>
    void error(E e) noexcept {
      set_error(*out_, e);
    }
    void done() {
    }
  };
  template <class CPO, class... AN>
  void via(const CPO& fn, AN&&... an) {
    submit(
      ::folly::pushmi::schedule(this->via_fn_base_ref().exec_),
        impl<CPO, std::decay_t<AN>...>{
            fn, std::tuple<Out&, std::decay_t<AN>...>{*out_, (AN &&) an...}, out_});
  }

  template<class... VN>
  void value(VN&&... vn) {
    if (this->via_fn_base_ref().done_) {
      return;
    }
    via(set_value, (VN&&) vn...);
  }

  template<class E>
  void error(E&& e) noexcept {
    if (this->via_fn_base_ref().done_) {
      return;
    }
    this->via_fn_base_ref().done_ = true;
    via(set_error, (E&&) e);
  }

  void done() {
    if (this->via_fn_base_ref().done_) {
      return;
    }
    this->via_fn_base_ref().done_ = true;
    via(set_done);
  }

  template<class Up>
  void starting(Up&& up) {
    if (this->via_fn_base_ref().done_) {
      return;
    }
    via(set_starting, (Up&&) up);
  }
  std::shared_ptr<Out> out_;
};

template <class Out, class Exec>
auto make_via_fn_data(Out out, Exec ex) -> via_fn_data<Exec, Out> {
  return {std::move(out), std::move(ex)};
}

struct via_fn {
 private:
  template <class In, class Factory>
  struct submit_impl {
    Factory ef_;

    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires Receiver<Out>) //
    void operator()(SIn&& in, Out out) const {
      auto exec = ::folly::pushmi::make_strand(ef_);
      ::folly::pushmi::submit((In &&) in,
        make_via_fn_data(std::move(out), std::move(exec)));
    }
  };

  template <class Factory>
  struct adapt_impl {
    Factory ef_;

    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>) //
    auto operator()(In&& in) const {
      return ::folly::pushmi::detail::sender_from(
          (In&&)in,
          submit_impl<In&&, Factory>{ef_});
    }
  };

 public:
  PUSHMI_TEMPLATE(class Factory)
  (requires StrandFactory<Factory>)
  auto operator()(Factory ef) const {
    return adapt_impl<Factory>{std::move(ef)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::via_fn via{};
} // namespace operators

} // namespace pushmi
} // namespace folly
