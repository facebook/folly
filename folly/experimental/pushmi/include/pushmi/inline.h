#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "executor.h"

namespace pushmi {

class inline_time_executor {
  public:
    using properties = property_set<is_time<>, is_executor<>, is_single<>>;

    auto now() {
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

struct inlineEXF {
  inline_time_executor operator()(){
    return {};
  }
};

inline inline_time_executor inline_executor() {
  return {};
}

} // namespace pushmi
