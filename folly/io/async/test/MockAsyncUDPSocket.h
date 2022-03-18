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

#include <folly/io/async/AsyncUDPSocket.h>
#include <folly/portability/GMock.h>

namespace folly {
namespace test {

struct MockAsyncUDPSocket : public AsyncUDPSocket {
  explicit MockAsyncUDPSocket(EventBase* evb) : AsyncUDPSocket(evb) {}
  ~MockAsyncUDPSocket() override {}

  MOCK_METHOD(const SocketAddress&, address, (), (const));
  MOCK_METHOD(
      void,
      bind,
      (const SocketAddress&, AsyncUDPSocket::BindOptions bindOptions));
  MOCK_METHOD(void, setFD, (NetworkSocket, AsyncUDPSocket::FDOwnership));
  MOCK_METHOD(
      ssize_t, write, (const SocketAddress&, const std::unique_ptr<IOBuf>&));
  MOCK_METHOD(
      int,
      writem,
      (Range<SocketAddress const*>,
       const std::unique_ptr<folly::IOBuf>*,
       size_t));
  MOCK_METHOD(
      ssize_t,
      writeGSO,
      (const folly::SocketAddress&, const std::unique_ptr<folly::IOBuf>&, int));
  MOCK_METHOD(
      ssize_t, writev, (const SocketAddress&, const struct iovec*, size_t));
  MOCK_METHOD(void, resumeRead, (ReadCallback*));
  MOCK_METHOD(void, pauseRead, ());
  MOCK_METHOD(void, close, ());
  MOCK_METHOD(void, setDFAndTurnOffPMTU, ());
  MOCK_METHOD(NetworkSocket, getNetworkSocket, (), (const));
  MOCK_METHOD(void, setReusePort, (bool));
  MOCK_METHOD(void, setReuseAddr, (bool));
  MOCK_METHOD(void, dontFragment, (bool));
  MOCK_METHOD(void, setErrMessageCallback, (ErrMessageCallback*));
  MOCK_METHOD(void, connect, (const SocketAddress&));
  MOCK_METHOD(bool, isBound, (), (const));
  MOCK_METHOD(int, getGSO, ());
  MOCK_METHOD(bool, setGSO, (int));
  MOCK_METHOD(ssize_t, recvmsg, (struct msghdr*, int));
  MOCK_METHOD(
      int,
      recvmmsg,
      (struct mmsghdr*, unsigned int, unsigned int, struct timespec*));
  MOCK_METHOD(void, setCmsgs, (const SocketOptionMap&));
  MOCK_METHOD(void, appendCmsgs, (const SocketOptionMap&));
  MOCK_METHOD(
      void, applyOptions, (const SocketOptionMap&, SocketOptionKey::ApplyPos));
};

} // namespace test
} // namespace folly
