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

#include <errno.h>
#include <gtest/gtest.h>
#include "folly/eventfd.h"
#include <glog/logging.h>

using namespace folly;

TEST(EventFD, Basic) {
  int fd = eventfd(10, EFD_NONBLOCK);
  CHECK_ERR(fd);
  uint64_t val;
  ssize_t r;
  // Running this twice -- once with the initial value, and once
  // after a write()
  for (int attempt = 0; attempt < 2; attempt++) {
    val = 0;
    r = read(fd, &val, sizeof(val));
    CHECK_ERR(r);
    EXPECT_EQ(sizeof(val), r);
    EXPECT_EQ(10, val);
    r = read(fd, &val, sizeof(val));
    EXPECT_EQ(-1, r);
    EXPECT_EQ(EAGAIN, errno);
    val = 10;
    r = write(fd, &val, sizeof(val));
    CHECK_ERR(r);
    EXPECT_EQ(sizeof(val), r);
  }
  close(fd);
}

TEST(EventFD, Semaphore) {
  int fd = eventfd(10, EFD_NONBLOCK | EFD_SEMAPHORE);
  CHECK_ERR(fd);
  uint64_t val;
  ssize_t r;
  // Running this twice -- once with the initial value, and once
  // after a write()
  for (int attempt = 0; attempt < 2; attempt++) {
    val = 0;
    for (int i = 0; i < 10; i++) {
      r = read(fd, &val, sizeof(val));
      CHECK_ERR(r);
      EXPECT_EQ(sizeof(val), r);
      EXPECT_EQ(1, val);
    }
    r = read(fd, &val, sizeof(val));
    EXPECT_EQ(-1, r);
    EXPECT_EQ(EAGAIN, errno);
    val = 10;
    r = write(fd, &val, sizeof(val));
    CHECK_ERR(r);
    EXPECT_EQ(sizeof(val), r);
  }
  close(fd);
}

