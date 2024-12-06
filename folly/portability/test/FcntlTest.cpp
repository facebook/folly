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

#include <folly/portability/Fcntl.h>
#include <folly/portability/Unistd.h>

#include <gtest/gtest.h>

using namespace ::testing;

/// Test that open supports "/dev/null" on Windows.
///
/// Windows doesn't have a /dev/null, but it does have
/// NUL, which achieves the same result.
TEST(FcntlTest, OpenDevNull) {
  int fd = folly::fileops::open("/dev/null", O_RDWR);
  ASSERT_GE(fd, 0);

  ASSERT_EQ(3, folly::fileops::write(fd, "abc", 3));

  {
    char buf[4] = {1, 2, 3, 4};
    // Reads from /dev/null always return 0 for end-of-file (EOF).
    ASSERT_EQ(0, folly::fileops::read(fd, buf, 3));
    ASSERT_EQ(buf[0], 1);
    ASSERT_EQ(buf[1], 2);
    ASSERT_EQ(buf[2], 3);
    ASSERT_EQ(buf[3], 4);
  }

  ASSERT_EQ(0, folly::fileops::close(fd));
}
