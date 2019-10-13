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

#include <chrono>

#include <folly/experimental/pushmi/detail/functional.h>
#include <folly/experimental/pushmi/executor/concepts.h>
#include <folly/experimental/pushmi/executor/primitives.h>

namespace folly {
namespace pushmi {

struct systemNowF {
  auto operator()() { return std::chrono::system_clock::now(); }
};

struct priorityZeroF {
  auto operator()(){ return 0; }
};

PUSHMI_TEMPLATE(class Exec)
  (requires Strand<Exec>)
struct strandFactory {
  Exec ex_;
  strandFactory() = default;
  explicit strandFactory(Exec ex) : ex_(std::move(ex)) {}
  Exec operator()(){ return ex_; }
};

struct passDNF {
  PUSHMI_TEMPLATE(class Data)
    (requires TimeExecutor<Data>)
  auto operator()(Data& in) const noexcept {
    return ::folly::pushmi::now(in);
  }
};

struct passDZF {
  PUSHMI_TEMPLATE(class Data)
    (requires ConstrainedExecutor<Data>)
  auto operator()(Data& in) const noexcept {
    return ::folly::pushmi::top(in);
  }
};

template <class Fn>
struct on_executor_fn : overload_fn<Fn> {
  constexpr on_executor_fn() = default;
  using overload_fn<Fn>::overload_fn;
};

template <class Fn>
auto on_executor(Fn fn) -> on_executor_fn<Fn> {
  return on_executor_fn<Fn>{std::move(fn)};
}

template <class Fn>
struct on_make_strand_fn : overload_fn<Fn> {
  constexpr on_make_strand_fn() = default;
  using overload_fn<Fn>::overload_fn;
};

template <class Fn>
auto on_make_strand(Fn fn) -> on_make_strand_fn<Fn> {
  return on_make_strand_fn<Fn>{std::move(fn)};
}

template <class... Fns>
struct on_schedule_fn : overload_fn<Fns...> {
  constexpr on_schedule_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_schedule(Fns... fns) -> on_schedule_fn<Fns...> {
  return on_schedule_fn<Fns...>{std::move(fns)...};
}

template <class Fn>
struct on_now_fn : overload_fn<Fn> {
  constexpr on_now_fn() = default;
  using overload_fn<Fn>::overload_fn;
};

template <class Fn>
auto on_now(Fn fn) -> on_now_fn<Fn> {
  return on_now_fn<Fn>{std::move(fn)};
}

} // namespace pushmi
} // namespace folly
