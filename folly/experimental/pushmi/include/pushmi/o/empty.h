// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../deferred.h"
#include "../single_deferred.h"
#include "../detail/functional.h"

namespace pushmi {
namespace detail {
  template <class V>
  struct single_empty_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires SingleReceiver<Out, V>)
    void operator()(Out out) {
      ::pushmi::set_done(out);
    }
  };
  struct empty_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires NoneReceiver<Out>)
    void operator()(Out out) {
      ::pushmi::set_done(out);
    }
  };
}

namespace operators {
template <class V>
auto empty() {
  return make_single_deferred(detail::single_empty_impl<V>{});
}

inline auto empty() {
  return make_deferred(detail::empty_impl{});
}

} // namespace operators
} // namespace pushmi
