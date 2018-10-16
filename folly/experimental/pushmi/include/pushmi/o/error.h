// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "submit.h"
#include "extension_operators.h"

namespace pushmi {
namespace detail {
  struct single_error_sender_base : single_sender<ignoreSF, inlineEXF> {
    using properties = property_set<is_sender<>, is_single<>, is_always_blocking<>, is_fifo_sequence<>>;
  };
  template <class E, class... VN>
  struct single_error_impl {
    E e_;
    PUSHMI_TEMPLATE(class Out)
      (requires ReceiveError<Out, E> && ReceiveValue<Out, VN...>)
    void operator()(single_error_sender_base&, Out out) {
      ::pushmi::set_error(out, std::move(e_));
    }
  };
}

namespace operators {

PUSHMI_TEMPLATE(class... VN, class E)
  (requires And<SemiMovable<VN>...> && SemiMovable<E>)
auto error(E e) {
  return make_single_sender(detail::single_error_sender_base{}, detail::single_error_impl<E, VN...>{std::move(e)});
}

} // namespace operators
} // namespace pushmi
