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
  template <class Predicate>
  struct on_value_impl {
    Predicate p_;
    template <class Out, class V>
    void operator()(Out& out, V&& v) const {
      if (p_(as_const(v))) {
        ::pushmi::set_value(out, (V&&) v);
      } else {
        ::pushmi::set_done(out);
      }
    }
  };
  template <class In, class Predicate>
  struct out_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      return ::pushmi::detail::receiver_from_fn<In>()(
        std::move(out),
        // copy 'p' to allow multiple calls to submit
        ::pushmi::on_value(on_value_impl<Predicate>{p_})
      );
    }
  };
  template <class Predicate>
  struct in_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return ::pushmi::detail::deferred_from(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(out_impl<In, Predicate>{p_})
      );
    }
  };
public:
  PUSHMI_TEMPLATE(class Predicate)
    (requires SemiMovable<Predicate>)
  auto operator()(Predicate p) const {
    return in_impl<Predicate>{std::move(p)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::filter_fn filter{};
} // namespace operators

} // namespace pushmi
