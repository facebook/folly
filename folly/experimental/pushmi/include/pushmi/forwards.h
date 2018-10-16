#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <exception>
#include <chrono>
#include "traits.h"

namespace pushmi {

// property_set

template <class T, class = void>
struct property_traits;

template <class T>
struct property_set_traits;

template<class... PropertyN>
struct property_set;

// trait & tag types
template<class...TN>
struct is_silent;
template<class...TN>
struct is_none;
template<class...TN>
struct is_single;
template<class...TN>
struct is_many;

template<class...TN>
struct is_flow;

template<class...TN>
struct is_receiver;

template<class...TN>
struct is_sender;

template<class...TN>
struct is_time;
template<class...TN>
struct is_constrained;

// implementation types

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class none;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class deferred;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class single;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class many;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class single_deferred;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class many_deferred;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class time_single_deferred;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_single;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_single_deferred;

template<
  class E = std::exception_ptr,
  class TP = std::chrono::system_clock::time_point>
struct any_time_executor_ref;

namespace operators {}
namespace extension_operators {}
namespace aliases {
    namespace v = ::pushmi;
    namespace mi = ::pushmi;
    namespace op = ::pushmi::operators;
    namespace ep = ::pushmi::extension_operators;
}

} // namespace pushmi
