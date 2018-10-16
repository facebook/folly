#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../many_deferred.h"
#include "extension_operators.h"
#include "submit.h"

namespace pushmi {

PUSHMI_CONCEPT_DEF(
  template (class R)
  concept Range,
    requires (R&& r) (
      implicitly_convertible_to<bool>(std::begin(r) == std::end(r))
    )
);

namespace operators {

PUSHMI_INLINE_VAR constexpr struct from_fn {
private:
  template <class I, class S>
  struct out_impl {
    I begin_;
    S end_;
    PUSHMI_TEMPLATE(class Out)
      (requires ManyReceiver<Out, typename std::iterator_traits<I>::value_type>)
    void operator()(Out out) const {
      auto c = begin_;
      for (; c != end_; ++c) {
        ::pushmi::set_next(out, *c);
      }
      ::pushmi::set_done(out);
    }
  };
public:
  PUSHMI_TEMPLATE(class I, class S)
    (requires
      DerivedFrom<
          typename std::iterator_traits<I>::iterator_category,
          std::forward_iterator_tag>)
  auto operator()(I begin, S end) const {
    return make_many_deferred(out_impl<I, S>{begin, end});
  }

  PUSHMI_TEMPLATE(class R)
    (requires Range<R>)
  auto operator()(R&& range) const {
    return (*this)(std::begin(range), std::end(range));
  }
} from {};

} // namespace operators

} // namespace pushmi
