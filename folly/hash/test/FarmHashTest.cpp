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

#include <folly/hash/FarmHash.h>

#include <folly/portability/GTest.h>

TEST(farmhash, simple) {
  EXPECT_NE(
      folly::hash::farmhash::Hash("foo", 3),
      folly::hash::farmhash::Hash("bar", 3));

  EXPECT_NE(
      folly::hash::farmhash::Hash32("foo", 3),
      folly::hash::farmhash::Hash32("bar", 3));

  EXPECT_NE(
      folly::hash::farmhash::Hash64("foo", 3),
      folly::hash::farmhash::Hash64("bar", 3));

  EXPECT_NE(
      folly::hash::farmhash::Fingerprint32("foo", 3),
      folly::hash::farmhash::Fingerprint32("bar", 3));

  EXPECT_NE(
      folly::hash::farmhash::Fingerprint64("foo", 3),
      folly::hash::farmhash::Fingerprint64("bar", 3));
}
