/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/experimental/TestUtil.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::test;

TEST(TemporaryFile, Simple) {
  int fd = -1;
  char c = 'x';
  {
    TemporaryFile f;
    EXPECT_FALSE(f.path().empty());
    EXPECT_EQ('/', f.path()[0]);
    fd = f.fd();
    EXPECT_LE(0, fd);
    ssize_t r = write(fd, &c, 1);
    EXPECT_EQ(1, r);
  }

  // The file must have been closed.  This assumes that no other thread
  // has opened another file in the meanwhile, which is a sane assumption
  // to make in this test.
  ssize_t r = write(fd, &c, 1);
  int savedErrno = errno;
  EXPECT_EQ(-1, r);
  EXPECT_EQ(EBADF, savedErrno);
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}

