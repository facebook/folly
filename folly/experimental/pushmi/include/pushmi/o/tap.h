// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "extension_operators.h"
#include "../deferred.h"
#include "../single_deferred.h"
#include "../time_single_deferred.h"

namespace pushmi {

namespace operators {
namespace detail {
template <class SideEffects, class Out>
struct tap_ {
  SideEffects sideEffects;
  Out out;

  using receiver_category = single_tag;

  template <class V>
  requires SingleReceiver<SideEffects, const V&>&& SingleReceiver<Out, V>
  void value(V&& v) {
    ::pushmi::set_value(sideEffects, const_cast<const V&>(v));
    ::pushmi::set_value(out, (V&&) v);
  }
  template <class E>
  requires NoneReceiver<SideEffects, E>&& NoneReceiver<Out, E>
  void error(E e) noexcept {
    ::pushmi::set_error(sideEffects, const_cast<const E&>(e));
    ::pushmi::set_error(out, std::move(e));
  }
  void done() {
    ::pushmi::set_done(sideEffects);
    ::pushmi::set_done(out);
  }
};

template <Receiver SideEffects, Receiver Out>
tap_(SideEffects, Out)->tap_<SideEffects, Out>;

struct tap_fn {
template <class... AN>
auto operator()(AN... an) const;
};
template <class... AN>
auto tap_fn::operator()(AN... an) const {
  auto args = std::tuple{std::move(an)...};
  return [args = std::move(args)]<class In>(In in) mutable {

      auto sideEffects{::pushmi::detail::out_from<In>(std::move(args))};
      using SideEffects = decltype(sideEffects);

      static_assert(
        ::pushmi::detail::deferred_requires_from<In, SideEffects,
          Receiver<SideEffects> &&
          SenderTo<In, SideEffects>,

          Receiver<SideEffects> &&
          SenderTo<In, SideEffects, single_tag>,

          Receiver<SideEffects> &&
          TimeSenderTo<In, SideEffects, single_tag> >(),
          "'In' is not deliverable to 'SideEffects'");

      return ::pushmi::detail::deferred_from<In, SideEffects>(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(
          [sideEffects = std::move(sideEffects)]<class Out>(Out out) {
            static_assert(
              ::pushmi::detail::deferred_requires_from<In, SideEffects,
                Receiver<Out> &&
                SenderTo<In, Out>,

                Receiver<Out> &&
                SenderTo<In, Out, single_tag>,

                Receiver<Out> &&
                TimeSenderTo<In, Out, single_tag> >(),
                "'In' is not deliverable to 'Out'");
            auto gang{::pushmi::detail::out_from<In>(detail::tap_{sideEffects, std::move(out)})};
            using Gang = decltype(gang);
            static_assert(
              ::pushmi::detail::deferred_requires_from<In, SideEffects,
                Receiver<Gang> &&
                SenderTo<In, Gang>,

                Receiver<Gang> &&
                SenderTo<In, Gang, single_tag>,

                Receiver<Gang> &&
                TimeSenderTo<In, Gang, single_tag> >(),
                "'In' is not deliverable to 'Out' & 'SideEffects'");
            return gang;
          }
        )
      );
    };
}

} // namespace detail

inline constexpr detail::tap_fn tap{};

} // namespace operators

} // namespace pushmi
