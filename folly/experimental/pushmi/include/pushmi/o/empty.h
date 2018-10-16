// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../detail/functional.h"
#include "submit.h"
#include "extension_operators.h"

namespace pushmi {
namespace detail {
  struct single_empty_sender_base : single_sender<ignoreSF, inlineEXF> {
    using properties = property_set<is_sender<>, is_single<>, is_always_blocking<>, is_fifo_sequence<>>;
  };
  template <class... VN>
  struct single_empty_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires ReceiveValue<Out, VN...>)
    void operator()(single_empty_sender_base&, Out out) {
      ::pushmi::set_done(out);
    }
  };
}

namespace operators {
template <class... VN>
auto empty() {
  return make_single_sender(detail::single_empty_sender_base{}, detail::single_empty_impl<VN...>{});
}

inline auto empty() {
  return make_single_sender(detail::single_empty_sender_base{}, detail::single_empty_impl<>{});
}

} // namespace operators
} // namespace pushmi
