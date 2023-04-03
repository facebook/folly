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

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/fdsock/SocketFds.h>

namespace folly {

/**
 * Intended for use with Unix sockets. Unlike regular `AsyncSocket`:
 *  - Can send FDs via `writeChainWithFds` using socket ancillary data (see
 *    `man cmsg`).
 *  - Always attempts to receive FDs included in incoming ancillary data.
 *    Groups of received FDs are enqueued to be retrieved via
 *    `popNextReceivedFds`.
 *  - The "read ancillary data" and "sendmsg params" callbacks are built-in
 *    are NOT customizable.
 *
 * Implementation limitation: Unlike regular `AsyncSocket`, this currently
 * does not automatically send socket timestamping events.  This could
 * easily be added, but seems of limited utility for Unix sockets.
 */
class AsyncFdSocket : public AsyncSocket {
 public:
  using UniquePtr = std::unique_ptr<AsyncSocket, Destructor>;

  /**
   * Create a new unconnected AsyncSocket.
   *
   * connect() must later be called on this socket to establish a connection.
   */
  explicit AsyncFdSocket(EventBase* evb);

  /**
   * Create a new AsyncSocket and begin the connection process.
   *
   * Unlike `AsyncSocket`, lacks `useZeroCopy` since Unix sockets do not
   * support zero-copy.
   */
  AsyncFdSocket(
      EventBase* evb,
      const folly::SocketAddress& address,
      uint32_t connectTimeout = 0);

  /**
   * Create a AsyncSocket from an already connected socket file descriptor.
   *
   * Unlike `AsyncSocket`, lacks `zeroCopyBufId` since Unix sockets do not
   * support zero-copy.
   */
  AsyncFdSocket(
      EventBase* evb,
      NetworkSocket fd,
      const folly::SocketAddress* peerAddress = nullptr);

  /**
   * `AsyncSocket::writeChain` analog that passes FDs as ancillary data over
   * the socket (see `man cmsg`).
   *
   * Invariants:
   *  - Max FDs per IOBuf: `SCM_MAX_FD` from include/net/scm.h, 253 for
   *    effectively all of Linux history.
   *  - FDs are received no earlier than the first data byte of the IOBuf,
   *    and no later than the last data byte. More specifically:
   *     - The currently implemented behavior is that FDs arrive precisely
   *       with the first data byte.  This was efficient and good enough for
   *       Thrift Rocket transport, where each message knows whether it
   *       expects FDs, and FDs are sent with the last data fragment of each
   *       message.
   *     - In other conceivable designs, it could be useful to pass FDs
   *       with the last data byte instead, which could be implemented as an
   *       optional write flag. In Thrift Rocket, this would minimize the
   *       buffering of FDs by the receiver, at the cost of more syscalls.
   */
  void writeChainWithFds(
      WriteCallback*,
      std::unique_ptr<folly::IOBuf>,
      SocketFds::ToSend,
      WriteFlags flags = WriteFlags::NONE);

  void setSendMsgParamCB(SendMsgParamsCallback*) override {
    LOG(DFATAL) << "AsyncFdSocket::setSendMsgParamCB is forbidden";
  }

// This uses no ancillary data callbacks on Windows, they wouldn't compile.
#if !defined(_WIN32)
 protected:
  void releaseIOBuf(
      std::unique_ptr<folly::IOBuf>, ReleaseIOBufCallback*) override;

 private:
  class FdSendMsgParamsCallback : public SendMsgParamsCallback {
   public:
    explicit FdSendMsgParamsCallback(folly::EventBase* evb) : eventBase_(evb) {}

    void getAncillaryData(
        folly::WriteFlags,
        void* data,
        const WriteRequestTag&,
        const bool byteEventsEnabled) noexcept override;

    uint32_t getAncillaryDataSize(
        folly::WriteFlags,
        const WriteRequestTag&,
        const bool byteEventsEnabled) noexcept override;

    void wroteBytes(const WriteRequestTag&) noexcept override;

   protected:
    friend class AsyncFdSocket;

    // Returns true on success, false if the tag was already registered, or
    // has a null IOBuf* (both are usage error).  The FDs are discarded on
    // error.
    bool registerFdsForWriteTag(WriteRequestTag, SocketFds::ToSend&&);

    // Called from `releaseIOBuf()` above, once we're sure that an IOBuf
    // will not be used for a write any more.  This is a good time to
    // discard the FDs associated with this write.
    //
    // CAREFUL: This may be invoked for IOBufs that never had a
    // corresponding `getAncillaryData*` call.
    void destroyFdsForWriteTag(WriteRequestTag) noexcept;

   private:
    using WriteTagToFds =
        std::unordered_map<WriteRequestTag, SocketFds::ToSend>;

    std::pair<size_t, WriteTagToFds::iterator> getCmsgSizeAndFds(
        const WriteRequestTag& writeTag) noexcept;

    const folly::EventBase* eventBase_; // only for dcheckIsInEventBaseThread

    WriteTagToFds writeTagToFds_;
  };

  void setUpCallbacks() noexcept;

  FdSendMsgParamsCallback sendMsgCob_;
#endif // !Windows
};

} // namespace folly
