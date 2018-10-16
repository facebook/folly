// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../single_sender.h"
#include "submit.h"
#include "extension_operators.h"

namespace pushmi {

namespace operators {

PUSHMI_INLINE_VAR constexpr struct just_fn {
private:
  struct sender_base : single_sender<ignoreSF, inlineEXF> {
    using properties = property_set<is_sender<>, is_single<>, is_always_blocking<>, is_fifo_sequence<>>;
  };
  template <class... VN>
  struct impl {
    std::tuple<VN...> vn_;
    PUSHMI_TEMPLATE (class Out)
      (requires ReceiveValue<Out, VN...>)
    void operator()(sender_base&, Out out) {
      ::pushmi::apply(::pushmi::set_value, std::tuple_cat(std::tuple<Out&>{out}, std::move(vn_)));
      ::pushmi::set_done(std::move(out));
    }
  };
public:
  PUSHMI_TEMPLATE(class... VN)
    (requires And<SemiMovable<VN>...>)
  auto operator()(VN... vn) const {
    return make_single_sender(sender_base{}, impl<VN...>{std::tuple<VN...>{std::move(vn)...}});
  }
} just {};
} // namespace operators

} // namespace pushmi
