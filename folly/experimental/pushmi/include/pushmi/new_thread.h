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

struct __new_thread_submit {
  template<Regular TP, Receiver Out>
  void operator()(TP at, Out out) const;
};

template<Regular TP, Receiver Out>
void __new_thread_submit::operator()(TP at, Out out) const {
  std::thread t{[at = std::move(at), out = std::move(out)]() mutable {
    auto tr = trampoline();
    ::pushmi::submit(tr, std::move(at), std::move(out));
  }};
  // pass ownership of thread to out
  t.detach();
}

inline auto new_thread() {
  return time_single_deferred{__new_thread_submit{}};
}

}
