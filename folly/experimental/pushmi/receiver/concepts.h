/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <utility>

#include <folly/experimental/pushmi/traits.h>
#include <folly/experimental/pushmi/receiver/primitives.h>

namespace folly {
namespace pushmi {

PUSHMI_CONCEPT_DEF(
  template(class R) //
  (concept Receiver)(R), //
    requires(R& r)( //
      set_done(r) //
    ) && //
    SemiMovable<std::decay_t<R>> &&
    True<typename receiver_traits<R>::receiver_category> &&
    DerivedFrom<typename receiver_traits<R>::receiver_category, receiver_tag>
);

PUSHMI_CONCEPT_DEF(
  template(class R, class... VN) //
  (concept ReceiveValue)(R, VN...), //
    requires(R& r)( //
      set_value(r, std::declval<VN&&>()...) //
    ) && //
    Receiver<R> && //
    And<MoveConstructible<VN>...>
);

PUSHMI_CONCEPT_DEF(
  template(class R, class E) //
  (concept ReceiveError)(R, E), //
    requires(R& r, E&& e)( //
        set_error(r, (E &&) e) //
    ) && //
    Receiver<R> && //
    MoveConstructible<E>
);

template <class R>
PUSHMI_PP_CONSTRAINED_USING(
  Receiver<R>,
  receiver_category_t =,
    typename receiver_traits<R>::receiver_category
);

// add concepts to support cancellation and rate control
//

PUSHMI_CONCEPT_DEF(
  template(class R) //
  (concept FlowReceiver)(R), //
    Receiver<R>&&
    DerivedFrom<receiver_category_t<R>, flow_receiver_tag>);

PUSHMI_CONCEPT_DEF(
  template(class R, class... VN) //
  (concept FlowReceiveValue)(R, VN...), //
    FlowReceiver<R>&& ReceiveValue<R, VN...>);

PUSHMI_CONCEPT_DEF(
  template(class R, class E = std::exception_ptr) //
  (concept FlowReceiveError)(R, E), //
    FlowReceiver<R>&& ReceiveError<R, E>);

PUSHMI_CONCEPT_DEF(
  template(class R, class Up) //
  (concept FlowUpTo)(R, Up), //
    requires(R& r, Up&& up)( //
        set_starting(r, (Up &&) up) //
    ) &&
    FlowReceiver<R>
);

} // namespace pushmi
} // namespace folly
