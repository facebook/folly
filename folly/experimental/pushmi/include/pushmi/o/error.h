// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../sender.h"
#include "submit.h"
#include "extension_operators.h"

namespace pushmi {
namespace detail {
  template <class E>
  struct error_impl {
    E e_;
    PUSHMI_TEMPLATE(class Out)
      (requires NoneReceiver<Out, E>)
    void operator()(Out out) {
      ::pushmi::set_error(out, std::move(e_));
    }
  };
  template <class V, class E>
  struct single_error_impl {
    E e_;
    PUSHMI_TEMPLATE(class Out)
      (requires SingleReceiver<Out, V, E>)
    void operator()(Out out) {
      ::pushmi::set_error(out, std::move(e_));
    }
  };
}

namespace operators {

PUSHMI_TEMPLATE(class E)
  (requires SemiMovable<E>)
auto error(E e) {
  return make_sender(detail::error_impl<E>{std::move(e)});
}

PUSHMI_TEMPLATE(class V, class E)
  (requires SemiMovable<V> && SemiMovable<E>)
auto error(E e) {
  return make_single_sender(detail::single_error_impl<V, E>{std::move(e)});
}

} // namespace operators
} // namespace pushmi
