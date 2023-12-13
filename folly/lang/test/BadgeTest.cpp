/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <type_traits>

#include <folly/lang/Badge.h>
#include <folly/portability/GTest.h>

namespace {

class FriendClass;
class OtherFriendClass;
class DummyClass;

using SingleBadge = folly::badge<FriendClass>;
using OtherSingleBadge = folly::badge<OtherFriendClass>;
using MultipleBadges = folly::any_badge<FriendClass, OtherFriendClass>;
using SubsetBadges = folly::any_badge<FriendClass, OtherFriendClass>;
using SupersetBadges =
    folly::any_badge<FriendClass, OtherFriendClass, DummyClass>;

class ProtectedClass {
 public:
  static void single(SingleBadge) {}
  static void multiple(MultipleBadges) {}
  static void subset(SubsetBadges badges) { superset(badges); }
  static void superset(SupersetBadges) {}
};

class FriendClass {
 public:
  static void single() {
    ProtectedClass::single({});
    folly::any_badge<FriendClass> badge = SingleBadge{};
    ProtectedClass::single(badge);
  }
  static void multiple() { ProtectedClass::multiple(SingleBadge{}); }
  static void subset() { ProtectedClass::subset(SingleBadge{}); }
};

class OtherFriendClass {
 public:
  static void multiple() { ProtectedClass::multiple(OtherSingleBadge{}); }
  static void subset() { ProtectedClass::subset(OtherSingleBadge{}); }
};

} // namespace

TEST(BadgeTest, testSingleBadge) {
  // check a badge cannot be constructed outside of the context
  EXPECT_FALSE(std::is_default_constructible_v<SingleBadge>);

  // check a badge can be constructed from the allowed context
  FriendClass::single();
}

TEST(BadgeTest, testMultipleBadges) {
  // check a badge cannot be constructed outside of the context
  EXPECT_FALSE(std::is_default_constructible_v<SingleBadge>);
  EXPECT_FALSE(std::is_default_constructible_v<OtherSingleBadge>);
  EXPECT_FALSE(std::is_default_constructible_v<MultipleBadges>);

  // check a badge can be constructed from the allowed context
  FriendClass::multiple();
  OtherFriendClass::multiple();
}

TEST(BadgeTest, testSubsetBadges) {
  // check a badge cannot be constructed outside of the context
  EXPECT_FALSE(std::is_default_constructible_v<SubsetBadges>);
  EXPECT_FALSE(std::is_default_constructible_v<SupersetBadges>);

  // check a badge subset can be converted to superset anywhere
  FriendClass::subset();
  OtherFriendClass::subset();
}
