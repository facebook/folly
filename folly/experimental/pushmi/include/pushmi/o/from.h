#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../many_deferred.h"
#include "extension_operators.h"
#include "submit.h"

namespace pushmi {

namespace operators {

PUSHMI_TEMPLATE(class O, class S)
  (requires
    ConvertibleTo<
            typename std::iterator_traits<O>::iterator_category,
            std::forward_iterator_tag> &&
    ConvertibleTo<
            typename std::iterator_traits<S>::iterator_category,
            std::forward_iterator_tag>)
auto from(O begin, S end) {
  return make_many_deferred(constrain(
      lazy::ManyReceiver<_1, typename std::iterator_traits<O>::value_type>,
      [begin = std::move(begin), end = std::move(end)](auto out) {
        auto c = begin;
        for (; c != end; ++c) {
          ::pushmi::set_next(out, *c);
        }
        ::pushmi::set_done(out);
      }));
}

PUSHMI_TEMPLATE(class R)
(requires requires (
  std::begin(std::declval<R&&>()),
  std::end(std::declval<R&&>())
))
auto from(R&& range) {
  return from(std::begin(range), std::end(range));
}

} // namespace operators

} // namespace pushmi
