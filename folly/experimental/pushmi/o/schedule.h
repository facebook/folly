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

#include <folly/experimental/pushmi/executor/concepts.h>
#include <folly/experimental/pushmi/executor/primitives.h>

namespace folly {
namespace pushmi {
namespace detail {

struct schedule_fn {
 private:
  // TODO - only move, move-only types..
  // if out can be copied, then schedule can be called multiple
  // times..
  struct fn {
    PUSHMI_TEMPLATE(class Exec)
    (requires Executor<Exec>) //
        auto
        operator()(Exec& ex) {
      return schedule(ex);
    }
  };

 public:
  auto operator()() const {
    return schedule_fn::fn{};
  }
};

struct schedule_at_fn {
 private:
  // TODO - only move, move-only types..
  // if out can be copied, then schedule can be called multiple
  // times..
  template <class CV>
  struct fn {
    CV at_;
    PUSHMI_TEMPLATE(class Exec)
    (requires ConstrainedExecutor<Exec>) //
        auto
        operator()(Exec& ex) {
      return schedule(ex, std::move(at_));
    }
  };

 public:
  template <class CV>
  auto operator()(CV&& at) const {
    return schedule_at_fn::fn<CV>{(CV &&) at};
  }
};

struct schedule_after_fn {
 private:
  // TODO - only move, move-only types..
  // if out can be copied, then schedule can be called multiple
  // times..
  template <class Dur>
  struct fn {
    Dur after_;
    PUSHMI_TEMPLATE(class Exec)
    (requires TimeExecutor<Exec>) //
        auto
        operator()(Exec& ex) {
      return schedule(ex, now(ex) + after_);
    }
  };

 public:
  template <class Dur>
  auto operator()(Dur&& after) const {
    return schedule_after_fn::fn<Dur>{(Dur &&) after};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::schedule_fn schedule{};
PUSHMI_INLINE_VAR constexpr detail::schedule_at_fn schedule_at{};
PUSHMI_INLINE_VAR constexpr detail::schedule_after_fn schedule_after{};
} // namespace operators

} // namespace pushmi
} // namespace folly
