#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "executor.h"

namespace pushmi {

class inline_constrained_executor_t {
  public:
    using properties = property_set<is_constrained<>, is_executor<>, is_single<>>;

    std::ptrdiff_t top() {
      return 0;
    }
    auto executor() { return *this; }
    PUSHMI_TEMPLATE(class CV, class Out)
      (requires Regular<CV> && Receiver<Out, is_single<>>)
    void submit(CV, Out out) {
      ::pushmi::set_value(std::move(out), *this);
    }
};

struct inlineConstrainedEXF {
  inline_constrained_executor_t operator()(){
    return {};
  }
};

inline inline_constrained_executor_t inline_constrained_executor() {
  return {};
}

class inline_time_executor_t {
  public:
    using properties = property_set<is_time<>, is_executor<>, is_single<>>;

    auto top() {
      return std::chrono::system_clock::now();
    }
    auto executor() { return *this; }
    PUSHMI_TEMPLATE(class TP, class Out)
      (requires Regular<TP> && Receiver<Out, is_single<>>)
    void submit(TP tp, Out out) {
      std::this_thread::sleep_until(tp);
      ::pushmi::set_value(std::move(out), *this);
    }
};

struct inlineTimeEXF {
  inline_time_executor_t operator()(){
    return {};
  }
};

inline inline_time_executor_t inline_time_executor() {
  return {};
}

class inline_executor_t {
  public:
    using properties = property_set<is_sender<>, is_executor<>, is_single<>>;

    auto executor() { return *this; }
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out, is_single<>>)
    void submit(Out out) {
      ::pushmi::set_value(std::move(out), *this);
    }
};

struct inlineEXF {
  inline_executor_t operator()(){
    return {};
  }
};

inline inline_executor_t inline_executor() {
  return {};
}

} // namespace pushmi
