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

struct new_thread_executor {
  using properties = property_set<is_sender<>, is_executor<>, is_single<>>;

  new_thread_executor executor() { return {}; }
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>)
  void submit(Out out) {
    std::thread t{[out = std::move(out)]() mutable {
      auto tr = ::pushmi::trampoline();
      ::pushmi::submit(tr, std::move(out));
    }};
    // pass ownership of thread to out
    t.detach();
  }
};

inline new_thread_executor new_thread() {
  return {};
}

} // namespace pushmi
