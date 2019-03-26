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

#include <folly/experimental/pushmi/executor.h>
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
struct via_fn_data : flow_receiver<>, via_fn_base<Exec> {
  via_fn_data(Out out, Exec exec)
      : via_fn_base<Exec>(std::move(exec)), out_(std::make_shared<Out>(std::move(out))) {}

  using properties = properties_t<Out>;
  using flow_receiver<>::value;
  using flow_receiver<>::error;
  using flow_receiver<>::done;

  template <class Up>
  struct impl {
    Up up_;
    std::shared_ptr<Out> out_;
    void operator()(any) {
      set_starting(out_, std::move(up_));
    }
  };
  template<class Up>
  void starting(Up&& up) {
    if (this->via_fn_base_ref().done_) {
      return;
    }
    submit(
      ::folly::pushmi::schedule(this->via_fn_base_ref().exec_),
        ::folly::pushmi::make_receiver(impl<std::decay_t<Up>>{
            (Up &&) up, out_}));
  }
  std::shared_ptr<Out> out_;
};

template <class Out, class Exec>
auto make_via_fn_data(Out out, Exec ex) -> via_fn_data<Exec, Out> {
  return {std::move(out), std::move(ex)};
}

struct via_fn {
 private:
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
      if (data.via_fn_base_ref().done_) {
        return;
      }
      submit(
        ::folly::pushmi::schedule(data.via_fn_base_ref().exec_),
          ::folly::pushmi::make_receiver(impl<std::decay_t<V>>{
              (V &&) v, data.out_}));
    }
  };
  template <class Out>
  struct on_error_impl {
    template <class E>
    struct impl {
      E e_;
      std::shared_ptr<Out> out_;
      void operator()(any) noexcept {
        set_error(out_, std::move(e_));
      }
    };
    template <class Data, class E>
    void operator()(Data& data, E e) const noexcept {
      if (data.via_fn_base_ref().done_) {
        return;
      }
      data.via_fn_base_ref().done_ = true;
      submit(
        ::folly::pushmi::schedule(data.via_fn_base_ref().exec_),
          ::folly::pushmi::make_receiver(
              impl<E>{std::move(e), std::move(data.out_)}));
    }
  };
  template <class Out>
  struct on_done_impl {
    struct impl {
      std::shared_ptr<Out> out_;
      void operator()(any) {
        set_done(out_);
      }
    };
    template <class Data>
    void operator()(Data& data) const {
      if (data.via_fn_base_ref().done_) {
        return;
      }
      data.via_fn_base_ref().done_ = true;
      submit(
          ::folly::pushmi::schedule(data.via_fn_base_ref().exec_),
          ::folly::pushmi::make_receiver(
              impl{std::move(data.out_)}));
    }
  };
  template <class In, class Factory>
  struct submit_impl {
    Factory ef_;
    PUSHMI_TEMPLATE(class SIn, class Out)
    (requires Receiver<Out>) //
        void
        operator()(SIn&& in, Out out) const {
      auto exec = ::folly::pushmi::make_strand(ef_);
      ::folly::pushmi::submit(
          (In &&) in,
          ::folly::pushmi::detail::receiver_from_fn<std::decay_t<In>>()(
              make_via_fn_data(std::move(out), std::move(exec)),
              on_value_impl<Out>{},
              on_error_impl<Out>{},
              on_done_impl<Out>{}));
    }
  };
  template <class Factory>
  struct adapt_impl {
    Factory ef_;
    PUSHMI_TEMPLATE(class In)
    (requires Sender<In>) //
        auto
        operator()(In&& in) const {
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
