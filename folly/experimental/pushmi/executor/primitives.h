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

#include <utility>

#include <folly/experimental/pushmi/detail/concept_def.h>
#include <folly/experimental/pushmi/detail/functional.h>
#include <folly/lang/CustomizationPoint.h>

namespace folly {
namespace pushmi {
namespace _adl {
//
// support methods on a class reference
//

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(std::declval<SD&>().executor())) //
auto get_executor(SD& sd) //
    noexcept(noexcept(sd.executor())) {
  return sd.executor();
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(std::declval<SD&>().make_strand())) //
auto make_strand(SD& sd) //
    noexcept(noexcept(sd.make_strand())) {
  return sd.make_strand();
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(std::declval<SD&>().top())) //
auto top(SD& sd) //
    noexcept(noexcept(sd.top())) {
  return sd.top();
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(std::declval<SD&>().schedule())) //
auto schedule(SD& sd) //
    noexcept(noexcept(sd.schedule())) {
  return sd.schedule();
}

PUSHMI_TEMPLATE(class SD, class TP)
(requires //
 requires(std::declval<SD&>().schedule(
     std::declval<TP (&)(TP)>()(top(std::declval<SD&>()))))) //
auto schedule(SD& sd, TP tp) //
    noexcept(noexcept(sd.schedule(std::move(tp)))) {
  return sd.schedule(std::move(tp));
}

//
// support methods on a class pointer
//

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(get_executor(*std::declval<SD>()))) //
auto get_executor(SD&& sd) //
    noexcept(noexcept(get_executor(*sd))) {
  return get_executor(*sd);
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(make_strand(*std::declval<SD>()))) //
auto make_strand(SD&& sd) //
    noexcept(noexcept(make_strand(*sd))) {
  return make_strand(*sd);
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(top(*std::declval<SD>()))) //
auto top(SD&& sd) //
    noexcept(noexcept(top(*sd))) {
  return top(*sd);
}

PUSHMI_TEMPLATE(class SD)
(requires //
 requires(schedule(*std::declval<SD>()))) //
auto schedule(SD&& sd) //
    noexcept(noexcept(schedule(*sd))) {
  return schedule(*sd);
}

PUSHMI_TEMPLATE(class SD, class TP)
(requires //
 requires(schedule(
     *std::declval<SD>(),
     std::declval<TP (&)(TP)>()(top(std::declval<SD&>()))))) //
auto schedule(SD&& sd, TP tp) //
    noexcept(noexcept(schedule(*sd, std::move(tp)))) {
  return schedule(*sd, std::move(tp));
}

//
// support a nullary function as a StrandFactory
//

PUSHMI_TEMPLATE(class S)
(requires Invocable<S&>) //
auto make_strand(S& s) //
    noexcept(noexcept(s())) {
  return s();
}

//
// accessors for free functions in this namespace
//

struct get_executor_fn {
  PUSHMI_TEMPLATE(class SD)
  (requires requires(get_executor(std::declval<SD&>()))) //
  auto operator()(SD& sd) const //
      noexcept(noexcept(get_executor(sd))) {
    return get_executor(sd);
  }
};

struct make_strand_fn {
  PUSHMI_TEMPLATE(class SD)
  (requires requires(make_strand(std::declval<SD&>()))) //
  auto operator()(SD& sd) const //
      noexcept(noexcept(make_strand(sd))) {
    return make_strand(sd);
  }
};

struct do_schedule_fn {
  PUSHMI_TEMPLATE(class SD, class... VN)
  (requires //
   requires(schedule(std::declval<SD&>(), std::declval<VN>()...))) //
  auto operator()(SD& s, VN&&... vn) const //
      noexcept(noexcept(schedule(s, (VN &&) vn...))) {
    return schedule(s, (VN &&) vn...);
  }
};

struct get_top_fn {
  PUSHMI_TEMPLATE(class SD)
  (requires //
   requires(top(std::declval<SD&>()))) //
  auto operator()(SD& sd) const //
      noexcept(noexcept(top(sd))) {
    return top(sd);
  }
};

} // namespace _adl

FOLLY_DEFINE_CPO(_adl::get_executor_fn, get_executor)
FOLLY_DEFINE_CPO(_adl::make_strand_fn, make_strand)
FOLLY_DEFINE_CPO(_adl::do_schedule_fn, schedule)
FOLLY_DEFINE_CPO(_adl::get_top_fn, now)
FOLLY_DEFINE_CPO(_adl::get_top_fn, top)

} // namespace pushmi
} // namespace folly
