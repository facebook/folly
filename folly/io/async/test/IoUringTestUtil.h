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

#pragma once

#include <memory>
#include <string>

#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/IoUringBackend.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_LIBURING

namespace folly::test {

// io_uring rings are charged to RLIMIT_MEMLOCK, a budget shared by all
// processes of the same user on the host, so keep them small. The charge scales
// with the ring capacity and the provided-buffer count, not the buffer size.
IoUringBackend::Options ioUringOptions();
IoUringBackend::Options ioUringOptionsWithProvidedBuffers();

// Under CI load, other processes can transiently exhaust the RLIMIT_MEMLOCK
// budget, so retry until they release their rings.
std::unique_ptr<EventBase> makeIoUringEventBase(
    IoUringBackend::Options (*makeOptions)());

class IoUringTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!IoUringBackend::isAvailable()) {
      GTEST_SKIP() << "IoUringBackend not available";
    }
  }
};

IoUringBackend::IoUringStats ioUringStats(EventBase& evb);

std::string makePayload(size_t len);

class RetainingReadCallback : public AsyncTransport::ReadCallback {
 public:
  void getReadBuffer(void** buf, size_t* len) override;
  void readDataAvailable(size_t len) noexcept override;
  bool isBufferMovable() noexcept override { return true; }
  void readBufferAvailable(std::unique_ptr<IOBuf> buf) noexcept override;
  void readEOF() noexcept override {}
  void readErr(const AsyncSocketException& ex) noexcept override;

  std::string data() const;

 private:
  IOBufQueue received_{IOBufQueue::cacheChainLength()};
};

void receiveFromPeer(
    EventBase& evb,
    AsyncTransport::ReadCallback& rcb,
    const std::string& payload);

void sendToPeer(EventBase& evb, const std::string& payload, WriteFlags flags);

} // namespace folly::test

#endif
