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

#include <chrono>
#include <map>
#include <memory>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <folly/Optional.h>
#include <folly/SocketAddress.h>
#include <folly/experimental/io/IoUringBase.h>
#include <folly/futures/Future.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufIovecBuilder.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/EventHandler.h>
#include <folly/net/NetOpsDispatcher.h>
#include <folly/portability/Sockets.h>
#include <folly/small_vector.h>

namespace folly {

class IoUringBackend;

class AsyncDetachFdCallback {
 public:
  virtual ~AsyncDetachFdCallback() = default;
  virtual void fdDetached(
      NetworkSocket ns, std::unique_ptr<IOBuf> unread) noexcept = 0;
  virtual void fdDetachFail(const AsyncSocketException& ex) noexcept = 0;
};

#if __has_include(<liburing.h>)

class AsyncIoUringSocket : public AsyncSocketTransport {
 public:
  struct Options {
    Options()
        : allocateNoBufferPoolBuffer(defaultAllocateNoBufferPoolBuffer),
          multishotRecv(true) {}

    static std::unique_ptr<IOBuf> defaultAllocateNoBufferPoolBuffer();
    folly::Function<std::unique_ptr<IOBuf>()> allocateNoBufferPoolBuffer;
    folly::Optional<AsyncWriter::ZeroCopyEnableFunc> zeroCopyEnable;
    bool multishotRecv;
  };

  using UniquePtr = std::unique_ptr<AsyncIoUringSocket, Destructor>;
  explicit AsyncIoUringSocket(
      AsyncTransport::UniquePtr other,
      IoUringBackend* backend = nullptr,
      Options&& options = Options{});
  explicit AsyncIoUringSocket(
      AsyncSocket* sock,
      IoUringBackend* backend = nullptr,
      Options&& options = Options{});
  explicit AsyncIoUringSocket(
      EventBase* evb,
      IoUringBackend* backend = nullptr,
      Options&& options = Options{});
  explicit AsyncIoUringSocket(
      EventBase* evb,
      NetworkSocket ns,
      IoUringBackend* backend = nullptr,
      Options&& options = Options{});

  static bool supports(EventBase* backend);

  void connect(
      AsyncSocket::ConnectCallback* callback,
      const folly::SocketAddress& address,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
      SocketOptionMap const& options = emptySocketOptionMap,
      const SocketAddress& bindAddr = anyAddress(),
      const std::string& ifName = std::string()) noexcept;

  void connect(
      ConnectCallback* callback,
      const folly::SocketAddress& address,
      int timeout,
      SocketOptionMap const& options,
      const SocketAddress& bindAddr,
      const std::string& ifName) noexcept override {
    connect(
        callback,
        address,
        std::chrono::milliseconds(timeout),
        options,
        bindAddr,
        ifName);
  }

  std::chrono::nanoseconds getConnectTime() const {
    return connectEndTime_ - connectStartTime_;
  }

  // AsyncSocketBase
  EventBase* getEventBase() const override { return evb_; }

  // AsyncReader
  void setReadCB(ReadCallback* callback) override;

  ReadCallback* getReadCallback() const override {
    return readSqe_->readCallback();
  }
  std::unique_ptr<IOBuf> takePreReceivedData() override {
    return readSqe_->takePreReceivedData();
  }

  // AsyncWriter
  void write(WriteCallback*, const void*, size_t, WriteFlags = WriteFlags::NONE)
      override;
  void writev(
      WriteCallback*,
      const iovec*,
      size_t,
      WriteFlags = WriteFlags::NONE) override;
  void writeChain(
      WriteCallback* callback,
      std::unique_ptr<IOBuf>&& buf,
      WriteFlags flags) override;
  bool canZC(std::unique_ptr<IOBuf> const& buf) const;

  // AsyncTransport
  void close() override;
  void closeNow() override;
  void closeWithReset() override;
  void shutdownWrite() override;
  void shutdownWriteNow() override;

  bool good() const override;
  bool readable() const override { return good(); }
  bool error() const override;
  bool hangup() const override;

  bool connecting() const override {
    return connectSqe_ && connectSqe_->inFlight();
  }

  void attachEventBase(EventBase*) override;
  void detachEventBase() override;
  bool isDetachable() const override;

  uint32_t getSendTimeout() const override {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(writeTimeoutTime_)
            .count());
  }

  void setSendTimeout(uint32_t ms) override;

  void getLocalAddress(SocketAddress* address) const override;

  void getPeerAddress(SocketAddress*) const override;

  void setPreReceivedData(std::unique_ptr<IOBuf> data) override;
  void cacheAddresses() override;

  /**
   * @return True iff end of record tracking is enabled
   */
  bool isEorTrackingEnabled() const override { return false; }

  void setEorTracking(bool) override {
    // don't support this.
    // as far as I can see this is only used by AsyncSSLSocket, but TLS1.3
    // supercedes this so I think we can ignore it.
    throw std::runtime_error(
        "AsyncIoUringSocket::setEorTracking not supported");
  }

  size_t getAppBytesWritten() const override { return getRawBytesWritten(); }
  size_t getRawBytesWritten() const override { return bytesWritten_; }
  size_t getAppBytesReceived() const override { return getRawBytesReceived(); }
  size_t getRawBytesReceived() const override;

  virtual void addLifecycleObserver(
      LifecycleObserver* /* observer */) override {
    throw std::runtime_error(
        "AsyncIoUringSocket::addLifecycleObserver not supported");
  }

  bool removeLifecycleObserver(LifecycleObserver* /* observer */) override {
    throw std::runtime_error(
        "AsyncIoUringSocket::removeLifecycleObserver not supported");
  }

  FOLLY_NODISCARD std::vector<LifecycleObserver*> getLifecycleObservers()
      const override {
    return {};
  }

  const AsyncTransport* getWrappedTransport() const override { return nullptr; }

  // AsyncSocketTransport
  int setNoDelay(bool noDelay) override;
  int setSockOpt(
      int level, int optname, const void* optval, socklen_t optsize) override;

  std::string getSecurityProtocol() const override { return securityProtocol_; }
  std::string getApplicationProtocol() const noexcept override {
    return applicationProtocol_;
  }
  NetworkSocket getNetworkSocket() const override { return fd_; }

  void setSecurityProtocol(std::string s) { securityProtocol_ = std::move(s); }
  void setApplicationProtocol(std::string s) {
    applicationProtocol_ = std::move(s);
  }

  void asyncDetachFd(AsyncDetachFdCallback* callback);
  bool readSqeInFlight() const { return readSqe_->inFlight(); }
  bool getTFOSucceded() const override;
  void enableTFO() override {
    // No-op if folly does not allow tfo
#if FOLLY_ALLOW_TFO
    DVLOG(5) << "AsyncIoUringSocket::enableTFO()";
    enableTFO_ = true;
#endif
  }

  void appendPreReceive(std::unique_ptr<IOBuf> iobuf) noexcept;

 protected:
  ~AsyncIoUringSocket() override;

 private:
  friend class ReadSqe;
  friend class WriteSqe;
  void setFd(NetworkSocket ns);
  void registerFd();
  void unregisterFd();
  void readProcessSubmit(
      struct io_uring_sqe* sqe,
      IoUringBufferProviderBase* bufferProvider,
      size_t* maxSize,
      IoUringBufferProviderBase* usedBufferProvider) noexcept;
  void readCallback(
      int res,
      uint32_t flags,
      size_t maxSize,
      IoUringBufferProviderBase* bufferProvider) noexcept;
  void allowReads();
  void previousReadDone();
  void processWriteQueue() noexcept;
  void setStateEstablished();
  void writeDone() noexcept;
  void doSubmitWrite() noexcept;
  void doReSubmitWrite() noexcept;
  void submitRead(bool now = false);
  void processConnectSubmit(
      struct io_uring_sqe* sqe, sockaddr_storage& storage);
  void processConnectResult(int i);
  void processConnectTimeout();
  void processFastOpenResult(int res, uint32_t flags) noexcept;
  void startSendTimeout();
  void sendTimeoutExpired();
  void failWrite(const AsyncSocketException& ex);
  void readEOF();
  void readError();
  NetworkSocket takeFd();
  bool setZeroCopy(bool enable) override;
  bool getZeroCopy() const override;
  void setZeroCopyEnableFunc(AsyncWriter::ZeroCopyEnableFunc func) override;

  enum class State {
    None,
    Connecting,
    Established,
    Closed,
    Error,
    FastOpen,
  };

  static std::string toString(State s);
  std::string stateAsString() const { return toString(state_); }

  struct ReadSqe : IoSqeBase, DelayedDestruction {
    using UniquePtr = std::unique_ptr<ReadSqe, Destructor>;
    explicit ReadSqe(AsyncIoUringSocket* parent);
    void processSubmit(struct io_uring_sqe* sqe) noexcept override;
    void callback(int res, uint32_t flags) noexcept override;
    void callbackCancelled(int, uint32_t) noexcept override;

    void setReadCallback(ReadCallback* callback, bool submitNow);
    ReadCallback* readCallback() const { return readCallback_; }

    size_t bytesReceived() const { return bytesReceived_; }

    std::unique_ptr<IOBuf> takePreReceivedData();
    void appendPreReceive(std::unique_ptr<IOBuf> data) noexcept {
      appendReadData(std::move(data), preReceivedData_);
    }

    void destroy() override {
      parent_ = nullptr;
      DelayedDestruction::destroy();
    }

    bool waitingForOldEventBaseRead() const;
    void setOldEventBaseRead(folly::SemiFuture<std::unique_ptr<IOBuf>>&& f) {
      oldEventBaseRead_ = std::move(f);
    }
    void attachEventBase();
    folly::Optional<folly::SemiFuture<std::unique_ptr<IOBuf>>>
    detachEventBase();

   private:
    ~ReadSqe() override = default;
    void appendReadData(
        std::unique_ptr<IOBuf> data, std::unique_ptr<IOBuf>& overflow) noexcept;
    void sendReadBuf(
        std::unique_ptr<IOBuf> buf, std::unique_ptr<IOBuf>& overflow) noexcept;
    bool readCallbackUseIoBufs() const;
    void invalidState(ReadCallback* callback);
    void processOldEventBaseRead();

    IoUringBufferProviderBase* lastUsedBufferProvider_;
    ReadCallback* readCallback_ = nullptr;
    AsyncIoUringSocket* parent_;
    size_t maxSize_;
    uint64_t setReadCbCount_{0};
    size_t bytesReceived_{0};

    std::unique_ptr<IOBuf> queuedReceivedData_;
    std::unique_ptr<IOBuf> preReceivedData_;
    std::unique_ptr<IOBuf> tmpBuffer_;
    bool supportsMultishotRecv_ =
        false; // todo: this can be per process instead of per socket

    folly::Optional<folly::SemiFuture<std::unique_ptr<IOBuf>>>
        oldEventBaseRead_;
    std::shared_ptr<folly::Unit> alive_;
  };

  struct CloseSqe : IoSqeBase {
    explicit CloseSqe(AsyncIoUringSocket* parent) : parent_(parent) {}
    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      parent_->closeProcessSubmit(sqe);
    }
    void callback(int, uint32_t) noexcept override { delete this; }
    void callbackCancelled(int, uint32_t) noexcept override { delete this; }
    AsyncIoUringSocket* parent_;
  };

  struct write_sqe_tag;
  using write_sqe_hook =
      boost::intrusive::list_base_hook<boost::intrusive::tag<write_sqe_tag>>;
  struct WriteSqe final : IoSqeBase, public write_sqe_hook {
    explicit WriteSqe(
        AsyncIoUringSocket* parent,
        WriteCallback* callback,
        std::unique_ptr<IOBuf>&& buf,
        WriteFlags flags,
        bool zc);
    ~WriteSqe() override { DVLOG(5) << "~WriteSqe() " << this; }

    void processSubmit(struct io_uring_sqe* sqe) noexcept override;
    void callback(int res, uint32_t flags) noexcept override;
    void callbackCancelled(int, uint32_t flags) noexcept override;
    int sendMsgFlags() const;
    std::pair<
        folly::SemiFuture<std::vector<std::pair<int, uint32_t>>>,
        WriteSqe*>
    detachEventBase();

    boost::intrusive::list_member_hook<> member_hook_;
    AsyncIoUringSocket* parent_;
    WriteCallback* callback_;
    std::unique_ptr<IOBuf> buf_;
    WriteFlags flags_;
    small_vector<struct iovec, 2> iov_;
    size_t totalLength_;
    struct msghdr msg_;

    bool zerocopy_{false};
    int refs_ = 1;
    folly::Function<bool(int, uint32_t)> detachedSignal_;
  };
  using WriteSqeList = boost::intrusive::list<
      WriteSqe,
      boost::intrusive::base_hook<write_sqe_hook>,
      boost::intrusive::constant_time_size<false>>;

  class WriteTimeout : public AsyncTimeout {
   public:
    explicit WriteTimeout(AsyncIoUringSocket* socket)
        : AsyncTimeout(socket->evb_), socket_(socket) {}

    void timeoutExpired() noexcept override { socket_->sendTimeoutExpired(); }

   private:
    AsyncIoUringSocket* socket_;
  };

  struct ConnectSqe : IoSqeBase, AsyncTimeout {
    explicit ConnectSqe(AsyncIoUringSocket* parent)
        : AsyncTimeout(parent->evb_), parent_(parent) {}
    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      parent_->processConnectSubmit(sqe, addrStorage);
    }
    void callback(int res, uint32_t) noexcept override {
      parent_->processConnectResult(res);
    }
    void callbackCancelled(int, uint32_t) noexcept override { delete this; }
    void timeoutExpired() noexcept override {
      if (!cancelled()) {
        parent_->processConnectTimeout();
      }
    }
    AsyncIoUringSocket* parent_;
    sockaddr_storage addrStorage;
  };

  struct FastOpenSqe : IoSqeBase {
    explicit FastOpenSqe(
        AsyncIoUringSocket* parent,
        SocketAddress const& addr,
        std::unique_ptr<AsyncIoUringSocket::WriteSqe> initialWrite);
    void processSubmit(struct io_uring_sqe* sqe) noexcept override;
    void cleanupMsg() noexcept;
    void callback(int res, uint32_t flags) noexcept override {
      cleanupMsg();
      parent_->processFastOpenResult(res, flags);
    }
    void callbackCancelled(int, uint32_t) noexcept override { delete this; }
    AsyncIoUringSocket* parent_;
    std::unique_ptr<AsyncIoUringSocket::WriteSqe> initialWrite;
    size_t addrLen_;
    sockaddr_storage addrStorage;
  };

  EventBase* evb_ = nullptr;
  NetworkSocket fd_;
  IoUringBackend* backend_ = nullptr;
  Options options_;
  mutable SocketAddress localAddress_;
  mutable SocketAddress peerAddress_;
  IoUringFdRegistrationRecord* fdRegistered_ = nullptr;
  int usedFd_ = -1;
  unsigned int mbFixedFileFlags_ = 0;
  std::unique_ptr<CloseSqe> closeSqe_{new CloseSqe(this)};

  State state_ = State::None;

  // read
  friend struct DetachFdState;
  ReadSqe::UniquePtr readSqe_;

  // write
  std::chrono::milliseconds writeTimeoutTime_{0};
  WriteTimeout writeTimeout_{this};
  WriteSqe* writeSqeActive_ = nullptr;
  WriteSqeList writeSqeQueue_;
  size_t bytesWritten_{0};

  // connect
  std::unique_ptr<ConnectSqe> connectSqe_;
  AsyncSocket::ConnectCallback* connectCallback_;
  std::chrono::milliseconds connectTimeout_{0};
  std::chrono::steady_clock::time_point connectStartTime_;
  std::chrono::steady_clock::time_point connectEndTime_;

  // stopTLS helpers:
  std::string securityProtocol_;
  std::string applicationProtocol_;

  // shutdown:
  int shutdownFlags_ = 0;

  // TCP fast open
  std::unique_ptr<FastOpenSqe> fastOpenSqe_;
  bool enableTFO_ = false;

  // detach event base
  bool isDetaching_ = false;
  Optional<SemiFuture<std::vector<std::pair<int, uint32_t>>>>
      detachedWriteResult_;
  std::shared_ptr<folly::Unit> alive_;

  void closeProcessSubmit(struct io_uring_sqe* sqe);
};

#endif

} // namespace folly
