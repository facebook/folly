#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "executor.h"
#include "trampoline.h"

namespace pushmi {

// very poor perf example executor.
//

struct new_thread_time_executor {
  using properties = property_set<is_time<>, is_executor<>, is_single<>>;

  auto now() {
    return std::chrono::system_clock::now();
  }
  new_thread_time_executor executor() { return {}; }
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Regular<TP> && Receiver<Out>)
  void submit(TP at, Out out) {
    std::thread t{[at = std::move(at), out = std::move(out)]() mutable {
      auto tr = ::pushmi::trampoline();
      ::pushmi::submit(tr, std::move(at), std::move(out));
    }};
    // pass ownership of thread to out
    t.detach();
  }
};

inline new_thread_time_executor new_thread() {
  return {};
}

} // namespace pushmi
