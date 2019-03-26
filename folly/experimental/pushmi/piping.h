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

#include <folly/experimental/pushmi/concepts.h>
#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {

struct pipeorigin {};

PUSHMI_TEMPLATE(class In, class Op)
(requires PUSHMI_EXP(lazy::Sender<In> PUSHMI_AND
    lazy::Invocable<Op&, In>)) //
    decltype(auto)
    operator|(In&& in, Op&& op) {
  return op((In &&) in);
}
PUSHMI_TEMPLATE(class Ex, class Op)
(requires PUSHMI_EXP(lazy::Executor<Ex> PUSHMI_AND
    lazy::Invocable<Op&, Ex>)) //
    decltype(auto)
    operator|(Ex&& ex, Op&& op) {
  return op((Ex &&) ex);
}
PUSHMI_TEMPLATE(class Out, class Op)
(requires PUSHMI_EXP(lazy::Receiver<Out> PUSHMI_AND
    lazy::Invocable<Op&, Out>)) //
    decltype(auto)
    operator|(Out&& out, Op&& op) {
  return op((Out &&) out);
}

PUSHMI_INLINE_VAR constexpr struct pipe_fn {
#if __cpp_fold_expressions >= 201603
  template <class T, class... FN>
  auto operator()(T&& t, FN&&... fn) const
      -> decltype(((T &&) t | ... | (FN &&) fn)) {
    return ((T &&) t | ... | (FN &&) fn);
  }
#else
  template <class T, class F>
  auto operator()(T&& t, F&& f) const -> decltype((T &&) t | (F &&) f) {
    return (T &&) t | (F &&) f;
  }
  template <class T, class F, class... FN, class This = pipe_fn>
  auto operator()(T&& t, F&& f, FN&&... fn) const
      -> decltype(This()(((T &&) t | (F &&) f), (FN &&) fn...)) {
    return This()(((T &&) t | (F &&) f), (FN &&) fn...);
  }
#endif
} const pipe{};

} // namespace pushmi
} // namespace folly
