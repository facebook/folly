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

#include <sys/types.h>

#include <chrono>
#include <map>
#include <memory>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <folly/Optional.h>
#include <folly/SocketAddress.h>
#include <folly/detail/SocketFastOpen.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufIovecBuilder.h>
#include <folly/io/ShutdownSocketSet.h>
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

#if __has_include(<liburing.h>)

class AsyncIoUringSocket : public AsyncTransport {
 public:
  using UniquePtr = std::unique_ptr<AsyncIoUringSocket, Destructor>;
  explicit AsyncIoUringSocket(
      AsyncTransport::UniquePtr other, IoUringBackend* backend = nullptr);
  explicit AsyncIoUringSocket(
      AsyncSocket* sock, IoUringBackend* backend = nullptr);
  explicit AsyncIoUringSocket(
      EventBase* evb, IoUringBackend* backend = nullptr);
  ~AsyncIoUringSocket() override;
  static bool supports(EventBase* backend);

  void connect(
      AsyncSocket::ConnectCallback* callback,
      const folly::SocketAddress& address,
      std::chrono::milliseconds timeout =
          std::chrono::milliseconds(0)) noexcept;

  std::chrono::nanoseconds getConnectTime() const {
    return connectEndTime_ - connectStartTime_;
  }

  // AsyncSocketBase
  EventBase* getEventBase() const override { return evb_; }

  // AsyncReader
  void setReadCB(ReadCallback* callback) override;

  ReadCallback* getReadCallback() const override { return readCallback_; }

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

  // AsyncTransport
  void close() override;
  void closeNow() override;
  void closeWithReset() override;
  void shutdownWrite() override {
    throw std::runtime_error("AsyncIoUringSocket::shutdownWrite not supported");
  }

  void shutdownWriteNow() override {
    throw std::runtime_error(
        "AsyncIoUringSocket::shutdownWriteNow not supported");
  }

  bool good() const override { return good_; }

  bool readable() const override { return good(); }

  bool connecting() const override {
    return connectSqe_ && connectSqe_->inFlight();
  }

  bool error() const override { return error_; }

  void attachEventBase(EventBase*) override {
    throw std::runtime_error(
        "AsyncIoUringSocket::attachEventBase not supported");
  }
  void detachEventBase() override {
    throw std::runtime_error(
        "AsyncIoUringSocket::detachEventBase not supported");
  }
  bool isDetachable() const override { return false; }

  uint32_t getSendTimeout() const override {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               writeTimeoutTime_)
        .count();
  }

  void setSendTimeout(uint32_t ms) override;

  void getLocalAddress(SocketAddress* address) const override;

  void getPeerAddress(SocketAddress*) const override;

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
  size_t getRawBytesReceived() const override { return bytesReceived_; }

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

 private:
  friend class ReadSqe;
  friend class WriteSqe;
  void setFd(NetworkSocket ns);
  bool readCallbackUseIoBufs() const;
  void appendPreReceive(std::unique_ptr<IOBuf> iobuf) noexcept;
  void readProcessSubmit(
      struct io_uring_sqe* sqe,
      IoUringBackend::ProvidedBufferProviderBase* bufferProvider,
      size_t* maxSize,
      bool* usedBufferProvider) noexcept;
  void readCallback(
      int res,
      uint32_t flags,
      size_t maxSize,
      IoUringBackend::ProvidedBufferProviderBase* bufferProvider) noexcept;
  void preReadCallback() noexcept;
  void writeDone() noexcept;
  void doSubmitWrite() noexcept;
  void doReSubmitWrite() noexcept;
  void submitRead(bool now = false);
  void processConnectSubmit(
      struct io_uring_sqe* sqe, sockaddr_storage& storage);
  void processConnectResult(int i);
  void processConnectTimeout();
  void startSendTimeout();
  void sendTimeoutExpired();
  void failWrite(const AsyncSocketException& ex);
  void sendReadBuf(std::unique_ptr<IOBuf> buf) noexcept;
  void invalidState(ReadCallback* callback);

  struct PreReadSqe : IoUringBackend::IoSqeBase {
    explicit PreReadSqe(AsyncIoUringSocket* parent) : parent_(parent) {}
    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      ::io_uring_prep_nop(sqe);
    }
    void callback(int, uint32_t) noexcept override {
      parent_->preReadCallback();
    }
    void callbackCancelled() noexcept override { delete this; }
    AsyncIoUringSocket* parent_;
  };

  struct ReadSqe : IoUringBackend::IoSqeBase {
    ReadSqe(
        AsyncIoUringSocket* parent,
        IoUringBackend::ProvidedBufferProviderBase* bufferProvider)
        : parent_(parent), bufferProvider_(bufferProvider) {}
    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      parent_->readProcessSubmit(
          sqe, bufferProvider_, &maxSize_, &lastUsedBufferProvider_);
    }
    void callback(int res, uint32_t flags) noexcept override {
      parent_->readCallback(
          res,
          flags,
          maxSize_,
          lastUsedBufferProvider_ ? bufferProvider_ : nullptr);
    }
    void callbackCancelled() noexcept override {
      if (!inFlight()) {
        delete this;
      }
    }

   private:
    AsyncIoUringSocket* parent_;
    IoUringBackend::ProvidedBufferProviderBase* bufferProvider_;
    size_t maxSize_;
    bool lastUsedBufferProvider_;
  };

  struct CloseSqe : IoUringBackend::IoSqeBase {
    explicit CloseSqe(AsyncIoUringSocket* parent) : parent_(parent) {}
    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      parent_->closeProcessSubmit(sqe);
    }
    void callback(int, uint32_t) noexcept override {}
    void callbackCancelled() noexcept override { delete this; }
    AsyncIoUringSocket* parent_;
  };

  struct write_sqe_tag;
  using write_sqe_hook =
      boost::intrusive::list_base_hook<boost::intrusive::tag<write_sqe_tag>>;
  struct WriteSqe final : IoUringBackend::IoSqeBase, public write_sqe_hook {
    explicit WriteSqe(
        AsyncIoUringSocket* parent,
        WriteCallback* callback,
        std::unique_ptr<IOBuf>&& buf,
        WriteFlags flags);
    ~WriteSqe() override { DVLOG(5) << "~WriteSqe() " << this; }

    void processSubmit(struct io_uring_sqe* sqe) noexcept override;
    void callback(int res, uint32_t flags) noexcept override;
    void callbackCancelled() noexcept override { delete this; }
    int sendMsgFlags() const;

    boost::intrusive::list_member_hook<> member_hook_;
    AsyncIoUringSocket* parent_;
    WriteCallback* callback_;
    std::unique_ptr<IOBuf> buf_;
    WriteFlags flags_;
    std::vector<struct iovec> iov_; // todo how many really
    size_t totalLength_;
    struct msghdr msg_;
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

  struct ConnectSqe : IoUringBackend::IoSqeBase, AsyncTimeout {
    explicit ConnectSqe(AsyncIoUringSocket* parent)
        : AsyncTimeout(parent->evb_), parent_(parent) {}
    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      parent_->processConnectSubmit(sqe, addrStorage);
    }
    void callback(int res, uint32_t) noexcept override {
      parent_->processConnectResult(res);
    }
    void callbackCancelled() noexcept override { delete this; }
    void timeoutExpired() noexcept override {
      if (!cancelled()) {
        parent_->processConnectTimeout();
      }
    }
    AsyncIoUringSocket* parent_;
    sockaddr_storage addrStorage;
  };

  EventBase* evb_ = nullptr;
  NetworkSocket fd_;
  IoUringBackend* backend_ = nullptr;
  mutable SocketAddress localAddress_;
  mutable SocketAddress peerAddress_;
  bool good_ = true;
  bool error_ = false;
  IoUringBackend::FdRegistrationRecord* fdRegistered_ = nullptr;
  int usedFd_ = -1;
  unsigned int mbFixedFileFlags_ = 0;
  std::unique_ptr<CloseSqe> closeSqe_{new CloseSqe(this)};

  // read
  bool supportsMultishotRecv_ =
      false; // todo: this can be per process instead of per socket
  std::unique_ptr<PreReadSqe> preReadSqe_{new PreReadSqe(this)};
  std::unique_ptr<ReadSqe> readSqe_;
  ReadCallback* readCallback_ = nullptr;
  uint64_t setReadCbCount_{0};
  std::unique_ptr<IOBuf> preReceivedData_;
  std::unique_ptr<IOBuf> tmpBuffer_;
  size_t bytesReceived_{0};

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

  void closeProcessSubmit(struct io_uring_sqe* sqe);
};

#endif

} // namespace folly
