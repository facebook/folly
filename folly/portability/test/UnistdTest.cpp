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

#include <folly/portability/Unistd.h>

#include <gtest/gtest.h>

using namespace ::testing;

/// Simple smoke test of `folly::fileops::{pipe, read, write, close}` functions.
///
/// On windows, the file descriptors created will be unix sockets, which is
/// normally unsupported by the windows UCRT version of these functions.
///
/// This tests pipe's ability to perform a write, read, and close.
TEST(UnistdTest, FileOpsSmokeTest) {
  int fds[2] = {-1};
  ASSERT_EQ(0, folly::fileops::pipe(fds));
  int readEnd = fds[0];
  int writeEnd = fds[1];
  ASSERT_GE(readEnd, 0);
  ASSERT_GE(writeEnd, 0);

  ASSERT_EQ(4, folly::fileops::write(writeEnd, "pika", 4));

  {
    char actual[5] = {0};
    ASSERT_EQ(4, folly::fileops::read(readEnd, actual, 4));
    ASSERT_STREQ("pika", actual);
  }

  ASSERT_EQ(0, folly::fileops::close(readEnd));
  ASSERT_EQ(0, folly::fileops::close(writeEnd));
}
