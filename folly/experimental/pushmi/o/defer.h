// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <folly/experimental/pushmi/receiver.h>
#include <folly/experimental/pushmi/single_sender.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/extension_operators.h>

namespace pushmi {

namespace operators {

PUSHMI_INLINE_VAR constexpr struct defer_fn {
private:
  template <class F>
  struct impl {
    F f_;
    PUSHMI_TEMPLATE(class Data, class Out)
      (requires Receiver<Out>)
    void operator()(Data&, Out out) {
      auto sender = f_();
      ::pushmi::submit(sender, std::move(out));
    }
  };
public:
  PUSHMI_TEMPLATE(class F)
    (requires Invocable<F&>)
  auto operator()(F f) const {
    struct sender_base : single_sender<> {
      using properties = properties_t<invoke_result_t<F&>>;
    };
    return make_single_sender(sender_base{}, impl<F>{std::move(f)});
  }
} defer {};

} // namespace operators

} // namespace pushmi
