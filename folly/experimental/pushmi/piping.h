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

#include <folly/experimental/pushmi/traits.h>

namespace folly {
namespace pushmi {

PUSHMI_TEMPLATE_DEBUG(class In, class Op)
(requires PUSHMI_EXP(lazy::Invocable<Op, In>)) //
    decltype(auto)
    operator|(In&& in, Op&& op) {
  return ((Op &&) op)((In &&) in);
}

PUSHMI_INLINE_VAR constexpr struct pipe_fn {
#if __cpp_fold_expressions >= 201603 && PUSHMI_NOT_ON_WINDOWS
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
