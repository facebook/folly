/*
 * Copyright 2015 Facebook, Inc.
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
#include <folly/wangle/channel/FileRegion.h>
#include <folly/io/async/test/AsyncSocketTest.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::wangle;
using namespace testing;

struct FileRegionTest : public Test {
  FileRegionTest() {
    // Connect
    socket = AsyncSocket::newSocket(&evb);
    socket->connect(&ccb, server.getAddress(), 30);

    // Accept the connection
    acceptedSocket = server.acceptAsync(&evb);
    acceptedSocket->setReadCB(&rcb);

    // Create temp file
    char path[] = "/tmp/AsyncSocketTest.WriteFile.XXXXXX";
    fd = mkostemp(path, O_RDWR);
    EXPECT_TRUE(fd > 0);
    EXPECT_EQ(0, unlink(path));
  }

  ~FileRegionTest() override {
    // Close up shop
    close(fd);
    acceptedSocket->close();
    socket->close();
  }

  TestServer server;
  EventBase evb;
  std::shared_ptr<AsyncSocket> socket;
  std::shared_ptr<AsyncSocket> acceptedSocket;
  ConnCallback ccb;
  ReadCallback rcb;
  int fd;
};

TEST_F(FileRegionTest, Basic) {
  size_t count = 1000000000; // 1 GB
  void* zeroBuf = calloc(1, count);
  write(fd, zeroBuf, count);

  FileRegion fileRegion(fd, 0, count);
  auto f = fileRegion.transferTo(socket);
  try {
    f.getVia(&evb);
  } catch (std::exception& e) {
    LOG(FATAL) << exceptionStr(e);
  }

  // Let the reads run to completion
  socket->shutdownWrite();
  evb.loop();

  ASSERT_EQ(rcb.state, STATE_SUCCEEDED);

  size_t receivedBytes = 0;
  for (auto& buf : rcb.buffers) {
    receivedBytes += buf.length;
    ASSERT_EQ(memcmp(buf.buffer, zeroBuf, buf.length), 0);
  }
  ASSERT_EQ(receivedBytes, count);
}

TEST_F(FileRegionTest, Repeated) {
  size_t count = 1000000;
  void* zeroBuf = calloc(1, count);
  write(fd, zeroBuf, count);

  int sendCount = 1000;

  FileRegion fileRegion(fd, 0, count);
  std::vector<Future<Unit>> fs;
  for (int i = 0; i < sendCount; i++) {
    fs.push_back(fileRegion.transferTo(socket));
  }
  auto f = collect(fs);
  ASSERT_NO_THROW(f.getVia(&evb));

  // Let the reads run to completion
  socket->shutdownWrite();
  evb.loop();

  ASSERT_EQ(rcb.state, STATE_SUCCEEDED);

  size_t receivedBytes = 0;
  for (auto& buf : rcb.buffers) {
    receivedBytes += buf.length;
  }
  ASSERT_EQ(receivedBytes, sendCount*count);
}
