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

#include <folly/SocketAddress.h>
#include <folly/io/IOBuf.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/net/NetworkSocket.h>

namespace folly {

class AsyncSocketTransport : public AsyncTransport {
 public:
  using UniquePtr = std::unique_ptr<AsyncSocketTransport, Destructor>;

  virtual int setNoDelay(bool noDelay) = 0;
  virtual int setSockOpt(
      int level, int optname, const void* optval, socklen_t optsize) = 0;
  virtual void setPreReceivedData(std::unique_ptr<IOBuf> data) = 0;
  virtual void cacheAddresses() = 0;

  class ConnectCallback {
   public:
    virtual ~ConnectCallback() = default;

    /**
     * connectSuccess() will be invoked when the connection has been
     * successfully established.
     */
    virtual void connectSuccess() noexcept = 0;

    /**
     * connectErr() will be invoked if the connection attempt fails.
     *
     * @param ex        An exception describing the error that occurred.
     */
    virtual void connectErr(const AsyncSocketException& ex) noexcept = 0;

    /**
     * preConnect() will be invoked just before the actual connect happens,
     *              default is no-ops.
     *
     * @param fd      An underneath created socket, use for connection.
     *
     */
    virtual void preConnect(NetworkSocket /*fd*/) {}
  };

  static const folly::SocketAddress& anyAddress();

  virtual void connect(
      ConnectCallback* callback,
      const folly::SocketAddress& address,
      int timeout = 0,
      SocketOptionMap const& options = emptySocketOptionMap,
      const folly::SocketAddress& bindAddr = anyAddress(),
      const std::string& ifName = "") noexcept = 0;

  virtual bool hangup() const = 0;
  void setPeerCertificate(
      std::unique_ptr<const AsyncTransportCertificate> cert) {
    peerCertData_ = std::move(cert);
  }

  const AsyncTransportCertificate* getPeerCertificate() const override {
    return peerCertData_.get();
  }

  void dropPeerCertificate() noexcept override { peerCertData_.reset(); }

  void setSelfCertificate(
      std::unique_ptr<const AsyncTransportCertificate> cert) {
    selfCertData_ = std::move(cert);
  }

  void dropSelfCertificate() noexcept override { selfCertData_.reset(); }

  const AsyncTransportCertificate* getSelfCertificate() const override {
    return selfCertData_.get();
  }

  virtual NetworkSocket getNetworkSocket() const = 0;
  virtual bool getTFOSucceded() const = 0;
  virtual void enableTFO() = 0;
  virtual void disableTransparentTls() {}

 protected:
  ~AsyncSocketTransport() override = default;

  // subclasses may cache these on first call to get
  mutable std::unique_ptr<const AsyncTransportCertificate> peerCertData_{
      nullptr};
  mutable std::unique_ptr<const AsyncTransportCertificate> selfCertData_{
      nullptr};
};

} // namespace folly
