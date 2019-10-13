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

#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/receiver/receiver.h>
#include <folly/experimental/pushmi/sender/single_sender.h>

namespace folly {
namespace pushmi {

namespace operators {

PUSHMI_INLINE_VAR constexpr struct defer_fn {
 private:
  template <class F>
  struct task : sender_traits<invoke_result_t<F&>> {
    using sender_type = invoke_result_t<F&>;
    F f_;
  public:
    using properties = properties_t<sender_type>;

    task() = default;
    explicit task(F f) : f_(std::move(f)) {}

    PUSHMI_TEMPLATE(class Out)
    (requires SenderTo<sender_type&, Out>)
    void submit(Out out) {
      auto sender = f_();
      pushmi::submit(sender, std::move(out));
    }
  };

 public:
  PUSHMI_TEMPLATE(class F)
  (requires Invocable<F&> && Sender<invoke_result_t<F&>>)
  auto operator()(F f) const {
    return task<F>{std::move(f)};
  }
} defer{};

} // namespace operators
} // namespace pushmi
} // namespace folly
