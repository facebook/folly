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
#include <folly/experimental/pushmi/detail/concept_def.h>
#include <folly/experimental/pushmi/sender/detail/concepts.h>
#include <folly/experimental/pushmi/executor/primitives.h>
#include <folly/experimental/pushmi/executor/properties.h>

namespace folly {
namespace pushmi {

// concepts to support execution
//

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept Executor)(Exec), //
  requires(Exec& exec)( //
    schedule(exec),
    requires_<SingleSender<decltype(schedule(exec))>>) &&
    SemiMovable<std::decay_t<Exec>>);

template <class Exec, class... Args>
PUSHMI_PP_CONSTRAINED_USING(
  Executor<Exec>,
  sender_t =,
    decltype(schedule(std::declval<Exec&>(), std::declval<Args>()...)));

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept ExecutorProvider)(Exec), //
  requires(Exec& exec)( //
    get_executor(exec),
    requires_<Executor<decltype(get_executor(exec))>>) &&
    SemiMovable<std::decay_t<Exec>>);

template <class S>
PUSHMI_PP_CONSTRAINED_USING(
  ExecutorProvider<S>,
  executor_t =,
    decltype(get_executor(std::declval<S&>())));

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept Strand)(Exec), //
    Executor<Exec>&&
    is_fifo_sequence_v<Exec>);

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept StrandFactory)(Exec), //
    requires(Exec& exec)( //
        make_strand(exec), //
        requires_<Strand<decltype(make_strand(exec))>>) &&
        SemiMovable<std::decay_t<Exec>>);

template <class Exec>
PUSHMI_PP_CONSTRAINED_USING(
  StrandFactory<Exec>,
  strand_t =,
    decltype(make_strand(std::declval<Exec&>())));

// add concepts for execution constraints
//
// the constraint could be time or priority enum or any other
// ordering constraint value-type.
//
// top() returns the constraint value that will cause the item to run asap.
// So now() for time and NORMAL for priority.

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept ConstrainedExecutor)(Exec), //
  requires(Exec& exec)( //
    top(exec),
    requires_<Regular<decltype(top(exec))>>,
    schedule(exec, top(exec)),
    requires_<SingleSender<decltype(schedule(exec, top(exec)))>>) &&
    Executor<Exec>);

template <class Exec>
PUSHMI_PP_CONSTRAINED_USING(
  ConstrainedExecutor<Exec>,
  constraint_t =,
    decltype(top(std::declval<Exec&>())));

PUSHMI_CONCEPT_DEF(
  template(class Exec, class TP, class Duration) //
  concept TimeExecutorImpl2_, //
    requires(Exec& exec, TP tp, Duration d)( //
      requires_<SingleSender<decltype(exec.schedule(tp + d))>>,
      requires_<SingleSender<decltype(exec.schedule(d + tp))>>,
      requires_<SingleSender<decltype(exec.schedule(tp - d))>>,
      tp += d,
      tp -= d));

PUSHMI_CONCEPT_DEF(
  template(class Exec, class TP = decltype(now(std::declval<Exec&>()))) //
  (concept TimeExecutorImpl_)(Exec, TP), //
    Regular<TP> &&
    TimeExecutorImpl2_<Exec, TP, std::chrono::nanoseconds> &&
    TimeExecutorImpl2_<Exec, TP, std::chrono::microseconds> &&
    TimeExecutorImpl2_<Exec, TP, std::chrono::milliseconds> &&
    TimeExecutorImpl2_<Exec, TP, std::chrono::seconds> &&
    TimeExecutorImpl2_<Exec, TP, std::chrono::minutes> &&
    TimeExecutorImpl2_<Exec, TP, std::chrono::hours> &&
    TimeExecutorImpl2_<Exec, TP, decltype(TP{} - TP{})>);

PUSHMI_CONCEPT_DEF(
  template(class Exec) //
  (concept TimeExecutor)(Exec), //
    requires(Exec& exec)( //
      now(exec),
      schedule(exec, now(exec)),
      requires_<SingleSender<decltype(schedule(exec, now(exec)))>>) &&
      ConstrainedExecutor<Exec> &&
      TimeExecutorImpl_<Exec>);

template <class Exec>
PUSHMI_PP_CONSTRAINED_USING(
  TimeExecutor<Exec>,
  time_point_t =,
    decltype(now(std::declval<Exec&>())));

} // namespace pushmi
} // namespace folly
