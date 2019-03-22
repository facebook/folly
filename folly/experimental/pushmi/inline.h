/*
 * Copyright 2018-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <folly/experimental/pushmi/executor.h>

namespace folly {
namespace pushmi {

class inline_constrained_executor_t {
 public:
   using properties = property_set<is_fifo_sequence<>>;

 template<class CV>
 struct task {
   CV cv_;
  using properties = property_set<
      is_sender<>,
      is_always_blocking<>,
      is_single<>>;

    PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>) //
    void submit(Out out) && {
      set_value(out, inline_constrained_executor_t{});
      set_done(out);
    }
  };
  auto top() {
    return 0;
  }
  task<std::ptrdiff_t> schedule() {
    return {top()};
  }
  template<class CV>
  task<std::ptrdiff_t> schedule(CV cv) {
    return {cv};
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
 struct task {
   TP tp_;
  using properties = property_set<
      is_sender<>,
      is_always_blocking<>,
      is_single<>>;

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
  task<std::chrono::system_clock::time_point> schedule() {
    return {top()};
  }
  template<class TP>
  task<TP> schedule(TP tp) {
    return {tp};
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

  struct task {
    using properties = property_set<
        is_sender<>,
        is_always_blocking<>,
        is_single<>>;

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
  inline_executor_t operator()() {
    return {};
  }
};

inline inline_executor_t inline_executor() {
  return {};
}

} // namespace pushmi
} // namespace folly
