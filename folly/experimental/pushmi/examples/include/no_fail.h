#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <pushmi/single_deferred.h>
#include <pushmi/o/submit.h>

namespace pushmi {

namespace detail {

struct no_fail_fn {
private:
  struct on_error_impl {
    void operator()(any, any) noexcept {
      std::abort();
    }
  };
  template <class In>
  struct out_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      return ::pushmi::detail::receiver_from_fn<In>()(
        std::move(out),
        ::pushmi::on_error(on_error_impl{})
      );
    }
  };
  struct in_impl {
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return ::pushmi::detail::deferred_from(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(out_impl<In>{})
      );
    }
  };
public:
  auto operator()() const {
    return in_impl{};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::no_fail_fn no_fail{};
} // namespace operators

} // namespace pushmi
