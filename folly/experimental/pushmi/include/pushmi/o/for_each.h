#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "extension_operators.h"
#include "submit.h"

namespace pushmi {
namespace detail {

struct for_each_fn {
private:
  template<class... PN>
  struct subset { using properties = property_set<PN...>; };
  template<class In, class Out>
  struct Pull : Out {
    explicit Pull(Out out) : Out(std::move(out)) {}
    using properties = property_set_insert_t<properties_t<Out>, property_set<is_flow<>>>;
    std::function<void(ptrdiff_t)> pull;
    template<class V>
    void value(V&& v) {
      ::pushmi::set_value(static_cast<Out&>(*this), (V&&) v);
      pull(1);
    }
    PUSHMI_TEMPLATE(class Up)
      (requires Receiver<Up>)
    void starting(Up up){
      pull = [up = std::move(up)](std::ptrdiff_t requested) mutable {
        ::pushmi::set_value(up, requested);
      };
      pull(1);
    }
    PUSHMI_TEMPLATE(class Up)
      (requires ReceiveValue<Up>)
    void starting(Up up){}
  };
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In> && Flow<In> && Many<In>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<subset<is_sender<>, property_set_index_t<properties_t<In>, is_single<>>>>()(std::move(args_))};
      using Out = decltype(out);
      ::pushmi::submit(in, ::pushmi::detail::receiver_from_fn<In>()(Pull<In, Out>{std::move(out)}));
      return in;
    }
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In> && Constrained<In> && Flow<In> && Many<In>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<subset<is_sender<>, property_set_index_t<properties_t<In>, is_single<>>>>()(std::move(args_))};
      using Out = decltype(out);
      ::pushmi::submit(in, ::pushmi::top(in), ::pushmi::detail::receiver_from_fn<In>()(Pull<In, Out>{std::move(out)}));
      return in;
    }
  };
public:
  template <class... AN>
  auto operator()(AN&&... an) const {
    return for_each_fn::fn<AN...>{{(AN&&) an...}};
  }
};

} //namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::for_each_fn for_each{};
} //namespace operartors

} //namespace pushmi
