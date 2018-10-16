#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../piping.h"
#include "extension_operators.h"

namespace pushmi {

namespace detail {

struct filter_fn {
private:
  template <class In, class Predicate>
  struct on_value_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class Out, class... VN)
      (requires Receiver<Out>)
    void operator()(Out& out, VN&&... vn) const {
      if (p_(as_const(vn)...)) {
        ::pushmi::set_value(out, (VN&&) vn...);
      }
    }
  };
  template <class In, class Predicate>
  struct submit_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      return ::pushmi::detail::receiver_from_fn<In>()(
        std::move(out),
        // copy 'p' to allow multiple calls to submit
        on_value_impl<In, Predicate>{p_}
      );
    }
  };
  template <class Predicate>
  struct adapt_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return ::pushmi::detail::sender_from(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(submit_impl<In, Predicate>{p_})
      );
    }
  };
public:
  PUSHMI_TEMPLATE(class Predicate)
    (requires SemiMovable<Predicate>)
  auto operator()(Predicate p) const {
    return adapt_impl<Predicate>{std::move(p)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::filter_fn filter{};
} // namespace operators

} // namespace pushmi
