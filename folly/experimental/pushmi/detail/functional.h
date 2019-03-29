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

#include <folly/functional/Invoke.h>

#include <folly/experimental/pushmi/detail/concept_def.h>

namespace folly {
namespace pushmi {

/* using override */ using folly::invoke;

/* using override */ using folly::invoke_result;
/* using override */ using folly::invoke_result_t;
/* using override */ using folly::is_invocable;
/* using override */ using folly::is_invocable_r;
/* using override */ using folly::is_nothrow_invocable;
/* using override */ using folly::is_nothrow_invocable_r;

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept Invocable)(F, Args...),
    requires(F&& f) (
      ::folly::pushmi::invoke((F &&) f, std::declval<Args>()...)
    )
);

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept NothrowInvocable)(F, Args...),
    requires(F&& f) (
      requires_<noexcept(::folly::pushmi::invoke((F &&) f, std::declval<Args>()...))>
    ) &&
    Invocable<F, Args...>
);

} // namespace pushmi
} // namespace folly
