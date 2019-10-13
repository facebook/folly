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

#include <folly/executors/ScheduledExecutor.h>
#include <folly/experimental/pushmi/customizations/Executor.h>
#include <folly/experimental/pushmi/executor/executor.h>
#include <folly/experimental/pushmi/executor/time_executor.h>

namespace folly {

template <typename ExecutorT>
class ScheduledExecutorTask : public pushmi::single_sender_tag::with_values<
                                  Executor::KeepAlive<ExecutorT>>::no_error,
                              public pushmi::pipeorigin {
  using TimePoint = typename ScheduledExecutor::TimePoint;
  Executor::KeepAlive<ExecutorT> ex_;
  TimePoint at_;

 public:
  explicit ScheduledExecutorTask(
      ::folly::Executor::KeepAlive<ExecutorT> ex,
      TimePoint at)
      : ex_(std::move(ex)), at_(at) {}

  using properties = pushmi::property_set<pushmi::is_never_blocking<>>;

  template <class TaskReceiver>
  void submit(TaskReceiver&& out) && {
    // capturing ex into a function stored in ex is a circular reference.
    // the only way to break the reference is to destroy the function
    ExecutorT* ep = ex_.get();
    ep->scheduleAt(
        Func{[ex = std::move(ex_), pout = (TaskReceiver &&) out]() mutable {
          pushmi::set_value(pout, std::move(ex));
          pushmi::set_done(pout);
        }},
        std::move(at_));
  }
};

template <
    class ExecutorT,
    typename std::enable_if<
        std::is_base_of<::folly::ScheduledExecutor, ExecutorT>::value,
        int>::type = 0>
auto top(::folly::Executor::KeepAlive<ExecutorT>& se) {
  return se->now();
}
template <
    class ExecutorT,
    typename std::enable_if<
        std::is_base_of<::folly::ScheduledExecutor, ExecutorT>::value,
        int>::type = 0>
auto schedule(
    ::folly::Executor::KeepAlive<ExecutorT>& se,
    typename ScheduledExecutor::TimePoint at) {
  return ScheduledExecutorTask<ExecutorT>{se, std::move(at)};
}
template <
    class ExecutorT,
    typename std::enable_if<
        std::is_base_of<::folly::ScheduledExecutor, ExecutorT>::value,
        int>::type = 0>
auto schedule(
    ::folly::Executor::KeepAlive<ExecutorT>&& se,
    typename ScheduledExecutor::TimePoint at) {
  return ScheduledExecutorTask<ExecutorT>{std::move(se), std::move(at)};
}

// a derived class can disable the traits definitions by adding the following
//
// namespace pushmi {
// template <class T>
// struct property_set_traits_disable<
//     T,
//     ::folly::ScheduledExecutor,
//     typename std::enable_if<
//         std::is_base_of<Derived, T>::value>::type> : std::true_type {
// };
// } // namespace pushmi
//

namespace pushmi {
template <class T>
struct property_set_traits_disable<
    T,
    ::folly::Executor,
    typename std::enable_if<
        std::is_base_of<::folly::ScheduledExecutor, T>::value &&
        !property_set_traits_disable_v<T, ::folly::ScheduledExecutor>>::type>
    : std::true_type {};
template <class T>
struct property_set_traits<
    ::folly::Executor::KeepAlive<T>,
    typename std::enable_if<
        std::is_base_of<::folly::ScheduledExecutor, T>::value &&
        !property_set_traits_disable_v<T, ::folly::ScheduledExecutor>>::type> {
  using properties = property_set<is_fifo_sequence<>>;
};

} // namespace pushmi

} // namespace folly
