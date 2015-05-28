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

#pragma once

#include <folly/MoveWrapper.h>
#include <folly/io/IOBufQueue.h>
#include <folly/Memory.h>
#include <folly/io/async/AsyncUDPSocket.h>
#include <folly/io/async/EventBase.h>

namespace folly {

/**
 * UDP server socket
 *
 * It wraps a UDP socket waiting for packets and distributes them among
 * a set of event loops in round robin fashion.
 *
 * NOTE: At the moment it is designed to work with single packet protocols
 *       in mind. We distribute incoming packets among all the listeners in
 *       round-robin fashion. So, any protocol that expects to send/recv
 *       more than 1 packet will not work because they will end up with
 *       different event base to process.
 */
class AsyncUDPServerSocket : private AsyncUDPSocket::ReadCallback
                           , public AsyncSocketBase {
 public:
  class Callback {
   public:
    /**
     * Invoked when we start reading data from socket. It is invoked in
     * each acceptors/listeners event base thread.
     */
     virtual void onListenStarted() noexcept = 0;

    /**
     * Invoked when the server socket is closed. It is invoked in each
     * acceptors/listeners event base thread.
     */
     virtual void onListenStopped() noexcept = 0;

    /**
     * Invoked when a new packet is received
     */
    virtual void onDataAvailable(
      std::shared_ptr<AsyncUDPSocket> socket,
      const folly::SocketAddress& addr,
      std::unique_ptr<folly::IOBuf> buf,
      bool truncated) noexcept = 0;

    virtual ~Callback() = default;
  };

  /**
   * Create a new UDP server socket
   *
   * Note about packet size - We allocate buffer of packetSize_ size to read.
   * If packet are larger than this value, as per UDP protocol, remaining data
   * is dropped and you get `truncated = true` in onDataAvailable callback
   */
  explicit AsyncUDPServerSocket(EventBase* evb, size_t sz = 1500)
      : evb_(evb),
        packetSize_(sz),
        nextListener_(0) {
  }

  ~AsyncUDPServerSocket() {
    if (socket_) {
      close();
    }
  }

  void bind(const folly::SocketAddress& addy) {
    CHECK(!socket_);

    socket_ = std::make_shared<AsyncUDPSocket>(evb_);
    socket_->setReusePort(reusePort_);
    socket_->bind(addy);
  }

  void setReusePort(bool reusePort) {
    reusePort_ = reusePort;
  }

  folly::SocketAddress address() const {
    CHECK(socket_);
    return socket_->address();
  }

  void getAddress(SocketAddress* a) const {
    *a = address();
  }

  /**
   * Add a listener to the round robin list
   */
  void addListener(EventBase* evb, Callback* callback) {
    listeners_.emplace_back(evb, callback);
  }

  void listen() {
    CHECK(socket_) << "Need to bind before listening";

    for (auto& listener: listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread([callback] () mutable {
        callback->onListenStarted();
      });
    }

    socket_->resumeRead(this);
  }

  int getFD() const {
    CHECK(socket_) << "Need to bind before getting FD";
    return socket_->getFD();
  }

  void close() {
    CHECK(socket_) << "Need to bind before closing";
    socket_->close();
    socket_.reset();
  }

  EventBase* getEventBase() const {
    return evb_;
  }

 private:
  // AsyncUDPSocket::ReadCallback
  void getReadBuffer(void** buf, size_t* len) noexcept {
    std::tie(*buf, *len) = buf_.preallocate(packetSize_, packetSize_);
  }

  void onDataAvailable(const folly::SocketAddress& clientAddress,
                       size_t len,
                       bool truncated) noexcept {
    buf_.postallocate(len);
    auto data = buf_.split(len);

    if (listeners_.empty()) {
      LOG(WARNING) << "UDP server socket dropping packet, "
                   << "no listener registered";
      return;
    }

    if (nextListener_ >= listeners_.size()) {
      nextListener_ = 0;
    }

    auto client = clientAddress;
    auto callback = listeners_[nextListener_].second;
    auto mvp =
        folly::MoveWrapper<
            std::unique_ptr<folly::IOBuf>>(std::move(data));
    auto socket = socket_;

    // Schedule it in the listener's eventbase
    // XXX: Speed this up
    std::function<void()> f = [socket, client, callback, mvp, truncated] () mutable {
      callback->onDataAvailable(socket, client, std::move(*mvp), truncated);
    };

    listeners_[nextListener_].first->runInEventBaseThread(f);
    ++nextListener_;
  }

  void onReadError(const AsyncSocketException& ex) noexcept {
    LOG(ERROR) << ex.what();

    // Lets register to continue listening for packets
    socket_->resumeRead(this);
  }

  void onReadClosed() noexcept {
    for (auto& listener: listeners_) {
      auto callback = listener.second;

      listener.first->runInEventBaseThread([callback] () mutable {
        callback->onListenStopped();
      });
    }
  }

  EventBase* const evb_;
  const size_t packetSize_;

  std::shared_ptr<AsyncUDPSocket> socket_;

  // List of listener to distribute packets among
  typedef std::pair<EventBase*, Callback*> Listener;
  std::vector<Listener> listeners_;

  // Next listener to send packet to
  uint32_t nextListener_;

  // Temporary buffer for data
  folly::IOBufQueue buf_;

  bool reusePort_{false};
};

} // Namespace
