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

#if __has_include(<variant>)
#include <variant>
#endif

namespace folly {

/**
 * Badge pattern allows us to abstract over friend classes and make friend
 * feature more scoped. Using this simple technique we can specify a badge tag
 * on specific functions we want to gate for particular caller contexts (badge
 * holders). Badge can only be constructed by the specified badge holder binding
 * the tagged functions to that call site.
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
  friend Holder;
  badge() {}
};

#if __has_include(<variant>)
/**
 * For cases when multiple badge holders are required to call a function we can
 * use std::variant<...> over individual badges.
 *
 * Example:
 *   class ProtectedClass: {
 *    public:
 *     static void func(folly::badges<FriendClass, OtherFriendClass>);
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
using badges = std::variant<badge<Holders>...>;
#endif

} // namespace folly
