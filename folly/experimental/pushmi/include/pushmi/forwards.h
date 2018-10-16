#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "traits.h"

namespace pushmi {

// tag types
struct silent_tag;
struct none_tag;
struct single_tag;
struct flow_tag;

template <class>
struct sender_traits;

template <class>
struct receiver_traits;

template <SemiMovable... TN>
class none;

template <SemiMovable... TN>
class deferred;

template <SemiMovable... TN>
class single;

template <SemiMovable... TN>
class single_deferred;

template <SemiMovable... TN>
class time_single_deferred;

template <SemiMovable... TN>
class flow_single;

template <SemiMovable... TN>
class flow_single_deferred;

template<class E, class TP, int i = 0>
struct any_time_executor_ref;

namespace operators {}
namespace extension_operators {}
namespace aliases {
    namespace v = ::pushmi;
    namespace op = ::pushmi::operators;
    namespace ep = ::pushmi::extension_operators;
}

} // namespace pushmi
