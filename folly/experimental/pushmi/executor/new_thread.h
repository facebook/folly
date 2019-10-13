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
#include <folly/experimental/pushmi/executor/trampoline.h>
#include <folly/experimental/pushmi/sender/properties.h>
#include <folly/experimental/pushmi/executor/properties.h>

namespace folly {
namespace pushmi {

// very poor perf example executor.
//

struct new_thread_executor;

struct new_thread_task
: single_sender_tag::with_values<any_executor_ref<std::exception_ptr>> {
  using properties = property_set<is_never_blocking<>>;

  PUSHMI_TEMPLATE(class Out)
  (requires Receiver<Out>)
  void submit(Out out) && {
    std::thread t{[out = std::move(out)]() mutable {
      auto tr = ::folly::pushmi::trampoline();
      ::folly::pushmi::submit(::folly::pushmi::schedule(tr), std::move(out));
    }};
    // pass ownership of thread to out
    t.detach();
  }
};

struct new_thread_executor {
  using properties = property_set<is_concurrent_sequence<>>;

  new_thread_task schedule() {
    return {};
  }
};


inline new_thread_executor new_thread() {
  return {};
}

} // namespace pushmi
} // namespace folly
