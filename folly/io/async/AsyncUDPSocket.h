/*
 * Copyright 2014-present Facebook, Inc.
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

#include <memory>

#include <folly/ScopeGuard.h>
#include <folly/SocketAddress.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/AsyncSocketBase.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>

namespace folly {

/**
 * UDP socket
 */
class AsyncUDPSocket : public EventHandler {
 public:
  enum class FDOwnership {
    OWNS,
    SHARED
  };

  class ReadCallback {
   public:
    /**
     * Invoked when the socket becomes readable and we want buffer
     * to write to.
     *
     * NOTE: From socket we will end up reading at most `len` bytes
     *       and if there were more bytes in datagram, we will end up
     *       dropping them.
     */
     virtual void getReadBuffer(void** buf, size_t* len) noexcept = 0;

    /**
     * Invoked when a new datagraom is available on the socket. `len`
     * is the number of bytes read and `truncated` is true if we had
     * to drop few bytes because of running out of buffer space.
     */
    virtual void onDataAvailable(const folly::SocketAddress& client,
                                 size_t len,
                                 bool truncated) noexcept = 0;

    /**
     * Invoked when there is an error reading from the socket.
     *
     * NOTE: Since UDP is connectionless, you can still read from the socket.
     *       But you have to re-register readCallback yourself after
     *       onReadError.
     */
    virtual void onReadError(const AsyncSocketException& ex)
        noexcept = 0;

    /**
     * Invoked when socket is closed and a read callback is registered.
     */
    virtual void onReadClosed() noexcept = 0;

    virtual ~ReadCallback() = default;
  };

  /**
   * Create a new UDP socket that will run in the
   * given eventbase
   */
  explicit AsyncUDPSocket(EventBase* evb);
  ~AsyncUDPSocket() override;

  /**
   * Returns the address server is listening on
   */
  virtual const folly::SocketAddress& address() const {
    CHECK_NE(-1, fd_) << "Server not yet bound to an address";
    return localAddress_;
  }

  /**
   * Bind the socket to the following address. If port is not
   * set in the `address` an ephemeral port is chosen and you can
   * use `address()` method above to get it after this method successfully
   * returns.
   */
  virtual void bind(const folly::SocketAddress& address);

  /**
   * Use an already bound file descriptor. You can either transfer ownership
   * of this FD by using ownership = FDOwnership::OWNS or share it using
   * FDOwnership::SHARED. In case FD is shared, it will not be `close`d in
   * destructor.
   */
  virtual void setFD(int fd, FDOwnership ownership);

  /**
   * Send the data in buffer to destination. Returns the return code from
   * ::sendmsg.
   */
  virtual ssize_t write(const folly::SocketAddress& address,
                        const std::unique_ptr<folly::IOBuf>& buf);

  /**
   * Send data in iovec to destination. Returns the return code from sendmsg.
   */
  virtual ssize_t writev(const folly::SocketAddress& address,
                         const struct iovec* vec, size_t veclen);

  /**
   * Start reading datagrams
   */
  virtual void resumeRead(ReadCallback* cob);

  /**
   * Pause reading datagrams
   */
  virtual void pauseRead();

  /**
   * Stop listening on the socket.
   */
  virtual void close();

  /**
   * Get internal FD used by this socket
   */
  virtual int getFD() const {
    CHECK_NE(-1, fd_) << "Need to bind before getting FD out";
    return fd_;
  }

  /**
   * Set reuse port mode to call bind() on the same address multiple times
   */
  virtual void setReusePort(bool reusePort) {
    reusePort_ = reusePort;
  }

  /**
   * Set SO_REUSEADDR flag on the socket. Default is ON.
   */
  virtual void setReuseAddr(bool reuseAddr) {
    reuseAddr_ = reuseAddr;
  }

  /**
   * Set SO_RCVBUF option on the socket, if not zero. Default is zero.
   */
  virtual void setRcvBuf(int rcvBuf) {
    rcvBuf_ = rcvBuf;
  }

  /**
   * Set SO_SNDBUG option on the socket, if not zero. Default is zero.
   */
  virtual void setSndBuf(int sndBuf) {
    sndBuf_ = sndBuf;
  }

  /**
   * Set SO_BUSY_POLL option on the socket, if not zero. Default is zero.
   * Caution! The feature is not available on Apple's systems.
   */
  virtual void setBusyPoll(int busyPollUs) {
    busyPollUs_ = busyPollUs;
  }

  EventBase* getEventBase() const {
    return eventBase_;
  }

  /**
   * Enable or disable fragmentation on the socket.
   *
   * On Linux, this sets IP(V6)_MTU_DISCOVER to IP(V6)_PMTUDISC_DO when enabled,
   * and to IP(V6)_PMTUDISC_WANT when disabled. IP(V6)_PMTUDISC_WANT will use
   * per-route setting to set DF bit. It may be more desirable to use
   * IP(V6)_PMTUDISC_PROBE as opposed to IP(V6)_PMTUDISC_DO for apps that has
   * its own PMTU Discovery mechanism.
   * Note this doesn't work on Apple.
   */
  virtual void dontFragment(bool df);

 protected:
  virtual ssize_t sendmsg(int socket, const struct msghdr* message, int flags) {
    return ::sendmsg(socket, message, flags);
  }

  // Non-null only when we are reading
  ReadCallback* readCallback_;

 private:
  AsyncUDPSocket(const AsyncUDPSocket&) = delete;
  AsyncUDPSocket& operator=(const AsyncUDPSocket&) = delete;

  // EventHandler
  void handlerReady(uint16_t events) noexcept override;

  void handleRead() noexcept;
  bool updateRegistration() noexcept;

  EventBase* eventBase_;
  folly::SocketAddress localAddress_;

  int fd_;
  FDOwnership ownership_;

  // Temp space to receive client address
  folly::SocketAddress clientAddress_;

  bool reuseAddr_{true};
  bool reusePort_{false};
  int rcvBuf_{0};
  int sndBuf_{0};
  int busyPollUs_{0};
};

} // namespace folly
