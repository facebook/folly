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

template <class T, class = void>
struct property_set_traits;

template<class... PropertyN>
struct property_set;

// trait & tag types
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

template<class... TN>
struct is_executor;

template<class...TN>
struct is_time;
template<class...TN>
struct is_constrained;

template<class... TN>
struct is_always_blocking;

template<class... TN>
struct is_never_blocking;

template<class... TN>
struct is_maybe_blocking;

template<class... TN>
struct is_fifo_sequence;

template<class... TN>
struct is_concurrent_sequence;

// implementation types

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class receiver;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_receiver;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class many_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class constrained_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class time_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_many_sender;

template <
  class E = std::exception_ptr,
  class... VN>
class any_receiver;

template <
  class PE=std::exception_ptr,
  class PV=std::ptrdiff_t,
  class E=PE,
  class... VN>
class any_flow_receiver;

template<
  class E = std::exception_ptr,
  class... VN>
struct any_single_sender;

template<
  class E = std::exception_ptr,
  class... VN>
struct any_many_sender;

template <
  class PE=std::exception_ptr,
  class E=PE,
  class... VN>
class any_flow_single_sender;

template <
  class PE=std::exception_ptr,
  class PV=std::ptrdiff_t,
  class E=PE,
  class... VN>
class any_flow_many_sender;

template<
  class E = std::exception_ptr,
  class C = std::ptrdiff_t,
  class... VN>
struct any_constrained_single_sender;

template<
  class E = std::exception_ptr,
  class TP = std::chrono::system_clock::time_point,
  class... VN>
class any_time_single_sender;

template<
  class E = std::exception_ptr>
struct any_executor;

template<
  class E = std::exception_ptr>
struct any_executor_ref;

template<
  class E = std::exception_ptr,
  class CV = std::ptrdiff_t>
struct any_constrained_executor;

template<
  class E = std::exception_ptr,
  class TP = std::ptrdiff_t>
struct any_constrained_executor_ref;

template<
  class E = std::exception_ptr,
  class TP = std::chrono::system_clock::time_point>
struct any_time_executor;

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

namespace detail {
  struct any {
    template <class T>
    constexpr any(T&&) noexcept {}
  };
}

} // namespace pushmi
