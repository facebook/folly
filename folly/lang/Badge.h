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

#include <type_traits>

#include <folly/Traits.h>

namespace folly {

template <typename... Holders>
class any_badge;

/**
 * Badge pattern allows us to abstract over friend classes and make friend
 * feature more scoped. Using this simple technique we can specify a badge tag
 * on specific functions we want to gate for particular caller contexts (badge
 * holders). Badge can only be constructed by the specified badge holder binding
 * the tagged functions to that call site.
 *
 * As a rule, it is poor form to pass a badge to arbitrary code. It is poor form
 * for virtual, type-erased, or template functions to accept badges or to be
 * passed badges.
 *
 * Example:
 *   class ProtectedClass: {
 *     ...
 *    public:
 *     ...
 *     static void func(folly::badge<FriendClass>);
 *   };
 *
 *   void FriendClass::callProtectedFunc() {
 *     ProtectedClass::func({});               // compiles
 *   }
 *   void OtherClass::callProtectedFunc() {
 *     ProtectedClass::func({});               // does not compile!
 *   }
 *
 *     See: https://awesomekling.github.io/Serenity-C++-patterns-The-Badge/
 *  Author: Andreas Kling (https://github.com/awesomekling)
 *
 */
template <typename Holder>
class badge {
 public:
  friend Holder;
  /* implicit */ badge(any_badge<Holder>) noexcept {}

 private:
  /* implicit */ badge() noexcept {}
};

/**
 * For cases when multiple badge holders need to call a function we can
 * use folly::any_badge over each individual holder allowed.
 * We allow subsets of badges to lift into supersets:
 * folly::any_badge<A, B> lifts into folly::any_badge<A, B, C>.
 *
 * Example:
 *   class ProtectedClass: {
 *    public:
 *     static void func(folly::any_badge<FriendClass, OtherFriendClass>);
 *    };
 *
 *   void FriendClass::callProtectedFunc() {
 *     ProtectedClass::func(folly::badge<FriendClass>{});      // compiles
 *   }
 *   void OtherFriendClass::callProtectedFunc() {
 *     ProtectedClass::func(folly::badge<OtherFriendClass>{}); // compiles
 *   }
 */
template <typename... Holders>
class any_badge {
 public:
  template <
      typename Holder,
      typename = std::enable_if_t<folly::IsOneOf<Holder, Holders...>::value>>
  /* implicit */ any_badge(badge<Holder>) noexcept {}

  template <
      typename... OtherHolders,
      typename = std::enable_if_t<folly::StrictConjunction<
          folly::IsOneOf<OtherHolders, Holders...>...>::value>>
  /* implicit */ any_badge(any_badge<OtherHolders...>) noexcept {}
};

} // namespace folly
