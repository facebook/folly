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

#include <folly/portability/Filesystem.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace testing;

class FilesystemTest : public Test {};

TEST_F(FilesystemTest, lexically_normal) {
  //  from: https://en.cppreference.com/w/cpp/filesystem/path/lexically_normal,
  //      CC-BY-SA, GFDL
  EXPECT_THAT(
      folly::fs::lexically_normal("foo/./bar/..").native(),
      Eq(folly::fs::path("foo/").make_preferred().native()));
  EXPECT_THAT(
      folly::fs::lexically_normal("foo/.///bar/../").native(),
      Eq(folly::fs::path("foo/").make_preferred().native()));
}
