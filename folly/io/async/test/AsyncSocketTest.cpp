/*
 * Copyright 2014 Facebook, Inc.
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
#include <iostream>

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>

#include <gtest/gtest.h>

TEST(AsyncSocketTest, getSockOpt) {
  folly::EventBase evb;
  std::shared_ptr<folly::AsyncSocket> socket =
    folly::AsyncSocket::newSocket(&evb, 0);

  int val;
  socklen_t len;

  int expectedRc = getsockopt(socket->getFd(), SOL_SOCKET,
                              SO_REUSEADDR, &val, &len);
  int actualRc = socket->getSockOpt(SOL_SOCKET, SO_REUSEADDR, &val, &len);

  EXPECT_EQ(expectedRc, actualRc);
}
