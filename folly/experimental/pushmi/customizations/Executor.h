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

#include <folly/Executor.h>
#include <folly/experimental/pushmi/executor/executor.h>

namespace folly {

template <typename ExecutorT>
class ExecutorTask : public pushmi::single_sender_tag::with_values<
                         Executor::KeepAlive<ExecutorT>>::no_error,
                     public pushmi::pipeorigin {
  Executor::KeepAlive<ExecutorT> ex_;

 public:
  explicit ExecutorTask(Executor::KeepAlive<ExecutorT> ex)
      : ex_(std::move(ex)) {}

  // assume the worst - specialize pushmi::property_set_traits<> to strengthen
  using properties = pushmi::property_set<pushmi::is_maybe_blocking<>>;

  template <class TaskReceiver>
  void submit(TaskReceiver&& out) && {
    // capturing ex into a function stored in ex is a circular reference.
    // the only way to break the reference is to destroy the function
    ExecutorT* ep = ex_.get();
    ep->add([ex = std::move(ex_), pout = (TaskReceiver &&) out]() mutable {
      pushmi::set_value(pout, std::move(ex));
      pushmi::set_done(pout);
    });
  }
};

// a derived class can disable the traits definitions by adding the following
//
// namespace pushmi {
// template <class T>
// struct property_set_traits_disable<
//     T,
//     ::folly::Executor,
//     typename std::enable_if<
//         std::is_base_of<Derived, T>::value>::type> : std::true_type {
// };
// } // namespace pushmi
//

namespace pushmi {

template <class T>
struct property_set_traits<
    ::folly::Executor::KeepAlive<T>,
    typename std::enable_if<
        std::is_base_of<::folly::Executor, T>::value &&
        !property_set_traits_disable_v<T, ::folly::Executor>>::type> {
  // assume the worst - specialize pushmi::property_set_traits<> to strengthen
  // these
  using properties = property_set<is_concurrent_sequence<>>;
};

} // namespace pushmi

/// create a sender that will enqueue a call to value() and done() of the
/// receiver on this Executor's execution context. Value will be passed a
/// copy of this Executor. This adds support for Executors to compose with
/// other sender/receiver implementations including algorithms

template <
    class ExecutorT,
    typename std::enable_if<
        std::is_base_of<::folly::Executor, ExecutorT>::value,
        int>::type = 0>
ExecutorTask<ExecutorT> schedule(::folly::Executor::KeepAlive<ExecutorT>& se) {
  return ExecutorTask<ExecutorT>{se};
}

template <
    class ExecutorT,
    typename std::enable_if<
        std::is_base_of<::folly::Executor, ExecutorT>::value,
        int>::type = 0>
ExecutorTask<ExecutorT> schedule(::folly::Executor::KeepAlive<ExecutorT>&& se) {
  return ExecutorTask<ExecutorT>{std::move(se)};
}

} // namespace folly
