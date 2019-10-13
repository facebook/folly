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

#include <folly/experimental/pushmi/executor/executor.h>
#include <folly/experimental/pushmi/executor/properties.h>
#include <folly/experimental/pushmi/sender/properties.h>

namespace folly {
namespace pushmi {

class inline_constrained_executor_t {
 public:
  using properties = property_set<is_fifo_sequence<>>;

  template<class CV>
  struct task
  : single_sender_tag::with_values<inline_constrained_executor_t>::no_error {
  private:
    CV cv_;
  public:
    task() = default;
    explicit task(CV cv) : cv_(cv) {}
    using properties = property_set<is_always_blocking<>>;

    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>) //
    void submit(Out out) && {
      set_value(out, inline_constrained_executor_t{});
      set_done(out);
    }
  };
  std::ptrdiff_t top() {
    return 0;
  }
  auto schedule() {
    return task<std::ptrdiff_t>{top()};
  }
  template <class CV>
  auto schedule(CV cv) {
    return task<CV>{cv};
  }
};

struct inlineConstrainedEXF {
  inline_constrained_executor_t operator()() {
    return {};
  }
};

inline inline_constrained_executor_t inline_constrained_executor() {
  return {};
}

class inline_time_executor_t {
 public:
  using properties = property_set<is_fifo_sequence<>>;

  template<class TP>
  struct task : single_sender_tag::with_values<inline_time_executor_t>::no_error {
  private:
   TP tp_;
  public:
    using properties = property_set<is_always_blocking<>>;
    task() = default;
    explicit task(TP tp) : tp_(std::move(tp)) {}

    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>) //
    void submit(Out out) && {
      std::this_thread::sleep_until(tp_);
      set_value(out, inline_time_executor_t{});
      set_done(out);
    }
  };

  auto top() {
    return std::chrono::system_clock::now();
  }
  auto schedule() {
    return task<std::chrono::system_clock::time_point>{top()};
  }
  template<class TP>
  auto schedule(TP tp) {
    return task<TP>{tp};
  }
};

struct inlineTimeEXF {
  inline_time_executor_t operator()() {
    return {};
  }
};

inline inline_time_executor_t inline_time_executor() {
  return {};
}

class inline_executor_t {
  public:
    using properties = property_set<is_fifo_sequence<>>;

  struct task : single_sender_tag::with_values<inline_executor_t>::no_error {
    using properties = property_set<is_always_blocking<>>;

    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>) //
    void submit(Out out) && {
      set_value(out, inline_executor_t{});
      set_done(out);
    }
  };

  task schedule() {
    return {};
  }
};

struct inlineEXF {
  inline_executor_t operator()() const {
    return {};
  }
};

inline inline_executor_t inline_executor() {
  return {};
}

} // namespace pushmi
} // namespace folly
