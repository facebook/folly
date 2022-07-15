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
#include <set>
#include <vector>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>

#include <glog/logging.h>

#include <folly/CPortability.h>
#include <folly/Conv.h>
#include <folly/CppAttributes.h>
#include <folly/Function.h>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBaseBackendBase.h>
#include <folly/portability/Asm.h>
#include <folly/small_vector.h>

#if __has_include(<poll.h>)
#include <poll.h>
#endif

#if __has_include(<liburing.h>)
#include <liburing.h>
#endif

#if __has_include(<liburing.h>)

namespace folly {

class IoUringBackend : public EventBaseBackendBase {
 public:
  class FOLLY_EXPORT NotAvailable : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
  };

  struct Options {
    enum Flags {
      POLL_SQ = 0x1,
      POLL_CQ = 0x2,
      POLL_SQ_IMMEDIATE_IO = 0x4, // do not enqueue I/O operations
    };

    Options() = default;

    Options& setCapacity(size_t v) {
      capacity = v;
      return *this;
    }

    Options& setMinCapacity(size_t v) {
      minCapacity = v;

      return *this;
    }

    Options& setMaxSubmit(size_t v) {
      maxSubmit = v;

      return *this;
    }

    Options& setSqeSize(size_t v) {
      sqeSize = v;

      return *this;
    }

    Options& setMaxGet(size_t v) {
      maxGet = v;

      return *this;
    }

    Options& setUseRegisteredFds(size_t v) {
      registeredFds = v;
      return *this;
    }

    Options& setFlags(uint32_t v) {
      flags = v;

      return *this;
    }

    Options& setSQIdle(std::chrono::milliseconds v) {
      sqIdle = v;

      return *this;
    }

    Options& setCQIdle(std::chrono::milliseconds v) {
      cqIdle = v;

      return *this;
    }

    // Set the CPU as preferred for submission queue poll thread.
    //
    // This only has effect if POLL_SQ flag is specified.
    //
    // Can call multiple times to specify multiple CPUs.
    Options& setSQCpu(uint32_t v) {
      sqCpus.insert(v);

      return *this;
    }

    // Set the preferred CPUs for submission queue poll thread(s).
    //
    // This only has effect if POLL_SQ flag is specified.
    Options& setSQCpus(std::set<uint32_t> const& cpus) {
      sqCpus.insert(cpus.begin(), cpus.end());

      return *this;
    }

    Options& setSQGroupName(const std::string& v) {
      sqGroupName = v;

      return *this;
    }

    Options& setSQGroupNumThreads(size_t v) {
      sqGroupNumThreads = v;

      return *this;
    }

    Options& setInitialProvidedBuffers(size_t eachSize, size_t count) {
      initalProvidedBuffersCount = count;
      initalProvidedBuffersEachSize = eachSize;
      return *this;
    }

    Options& setRegisterRingFd(bool v) {
      registerRingFd = v;

      return *this;
    }

    Options& setTaskRunCoop(bool v) {
      taskRunCoop = v;

      return *this;
    }

    size_t capacity{256};
    size_t minCapacity{0};
    size_t maxSubmit{128};
    ssize_t sqeSize{-1};
    size_t maxGet{256};
    size_t registeredFds{0};
    bool registerRingFd{false};
    uint32_t flags{0};
    bool taskRunCoop{false};

    std::chrono::milliseconds sqIdle{0};
    std::chrono::milliseconds cqIdle{0};
    std::set<uint32_t> sqCpus;
    std::string sqGroupName;
    size_t sqGroupNumThreads{1};
    size_t initalProvidedBuffersCount{0};
    size_t initalProvidedBuffersEachSize{0};
  };

  struct IoSqeBase
      : boost::intrusive::list_base_hook<
            boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
    IoSqeBase() = default;
    // use raw addresses, so disallow copy/move
    IoSqeBase(IoSqeBase&&) = delete;
    IoSqeBase(const IoSqeBase&) = delete;
    IoSqeBase& operator=(IoSqeBase&&) = delete;
    IoSqeBase& operator=(const IoSqeBase&) = delete;

    virtual ~IoSqeBase() = default;
    virtual void processSubmit(struct io_uring_sqe* sqe) = 0;
    virtual void callback(int res, uint32_t flags) = 0;
    virtual void callbackCancelled() = 0;
    bool inFlight() const { return inFlight_; }
    bool cancelled() const { return cancelled_; }
    void markCancelled() { cancelled_ = true; }

   private:
    friend class IoUringBackend;
    void internalSubmit(struct io_uring_sqe* sqe);
    void internalCallback(int res, uint32_t flags);
    void internalUnmarkInflight();

    bool inFlight_ = false;
    bool cancelled_ = false;
  };

  class ProvidedBufferProviderBase {
   protected:
    uint16_t const gid_;
    int const count_;
    size_t const sizePerBuffer_;

   public:
    explicit ProvidedBufferProviderBase(
        uint16_t gid, uint32_t count, size_t sizePerBuffer)
        : gid_(gid), count_(count), sizePerBuffer_(sizePerBuffer) {}
    virtual ~ProvidedBufferProviderBase() = default;

    ProvidedBufferProviderBase(ProvidedBufferProviderBase&&) = delete;
    ProvidedBufferProviderBase(ProvidedBufferProviderBase const&) = delete;
    ProvidedBufferProviderBase& operator=(ProvidedBufferProviderBase&&) =
        delete;
    ProvidedBufferProviderBase& operator=(ProvidedBufferProviderBase const&) =
        delete;

    size_t sizePerBuffer() const { return sizePerBuffer_; }
    uint16_t gid() const { return gid_; }

    virtual uint32_t count() const = 0;
    virtual void unusedBuf(uint16_t i, size_t length) = 0;
    virtual std::unique_ptr<IOBuf> getIoBuf(uint16_t i, size_t length) = 0;
    virtual void enobuf() = 0;
    virtual bool available() const = 0;
  };

  explicit IoUringBackend(Options options);
  ~IoUringBackend() override;

  struct io_uring* ioRingPtr() {
    return &ioRing_;
  }

  // from EventBaseBackendBase
  event_base* getEventBase() override { return nullptr; }

  int eb_event_base_loop(int flags) override;
  int eb_event_base_loopbreak() override;

  int eb_event_add(Event& event, const struct timeval* timeout) override;
  int eb_event_del(Event& event) override;

  bool eb_event_active(Event&, int) override { return false; }

  // returns true if the current Linux kernel version
  // supports the io_uring backend
  static bool isAvailable();
  bool kernelHasNonBlockWriteFixes() const;

  struct FdRegistrationRecord : public boost::intrusive::slist_base_hook<
                                    boost::intrusive::cache_last<false>> {
    int count_{0};
    int fd_{-1};
    size_t idx_{0};
  };

  FdRegistrationRecord* registerFd(int fd) { return fdRegistry_.alloc(fd); }

  bool unregisterFd(FdRegistrationRecord* rec) { return fdRegistry_.free(rec); }

  // CQ poll mode loop callback
  using CQPollLoopCallback = folly::Function<void()>;

  void setCQPollLoopCallback(CQPollLoopCallback&& cb) {
    cqPollLoopCallback_ = std::move(cb);
  }

  // read/write/fsync/fdatasync file operation callback
  // int param is the io_uring_cqe res field
  // i.e. the result of the file operation
  using FileOpCallback = folly::Function<void(int)>;

  void queueRead(
      int fd,
      void* buf,
      unsigned int nbytes,
      off_t offset,
      FileOpCallback&& cb);

  void queueWrite(
      int fd,
      const void* buf,
      unsigned int nbytes,
      off_t offset,
      FileOpCallback&& cb);

  void queueReadv(
      int fd,
      Range<const struct iovec*> iovecs,
      off_t offset,
      FileOpCallback&& cb);

  void queueWritev(
      int fd,
      Range<const struct iovec*> iovecs,
      off_t offset,
      FileOpCallback&& cb);

  // there is no ordering between the prev submitted write
  // requests and the sync ops
  // ordering can be achieved by calling queue*sync from one of
  // the prev write callbacks, once all the write operations
  // we have to wait for are done
  void queueFsync(int fd, FileOpCallback&& cb);
  void queueFdatasync(int fd, FileOpCallback&& cb);

  void queueOpenat(
      int dfd, const char* path, int flags, mode_t mode, FileOpCallback&& cb);

  void queueOpenat2(
      int dfd, const char* path, struct open_how* how, FileOpCallback&& cb);

  void queueClose(int fd, FileOpCallback&& cb);

  void queueFallocate(
      int fd, int mode, off_t offset, off_t len, FileOpCallback&& cb);

  // sendmgs/recvmsg
  void queueSendmsg(
      int fd,
      const struct msghdr* msg,
      unsigned int flags,
      FileOpCallback&& cb);

  void queueRecvmsg(
      int fd, struct msghdr* msg, unsigned int flags, FileOpCallback&& cb);

  void submit(IoSqeBase& ioSqe) {
    // todo verify that the sqe is valid!
    submitImmediateIoSqe(ioSqe);
  }

  void submitSoon(IoSqeBase& ioSqe);
  void submitNow(IoSqeBase& ioSqe);
  void submitNowNoCqe(IoSqeBase& ioSqe, int count = 1);
  void cancel(IoSqeBase* sqe);

  // built in buffer provider
  ProvidedBufferProviderBase* bufferProvider() { return bufferProvider_.get(); }
  uint16_t nextBufferProviderGid() { return bufferProviderGidNext_++; }

 protected:
  enum class WaitForEventsMode { WAIT, DONT_WAIT };

  class SocketPair {
   public:
    SocketPair();

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    ~SocketPair();

    int readFd() const { return fds_[1]; }

    int writeFd() const { return fds_[0]; }

   private:
    std::array<int, 2> fds_{{-1, -1}};
  };

  struct UserData {
    uint64_t value;
    explicit UserData(uint64_t i) noexcept : value{i} {}
    template <
        typename T = int,
        std::enable_if_t<sizeof(void*) == sizeof(uint64_t), T> = 0>
    explicit UserData(void* p) noexcept
        : value{reinterpret_cast<uintptr_t>(p)} {}
    /* implicit */ operator uint64_t() const noexcept { return value; }
    template <
        typename T = int,
        std::enable_if_t<sizeof(void*) == sizeof(uint64_t), T> = 0>
    /* implicit */ operator void*() const noexcept {
      return reinterpret_cast<void*>(value);
    }
  };

  static uint32_t getPollFlags(short events) {
    uint32_t ret = 0;
    if (events & EV_READ) {
      ret |= POLLIN;
    }

    if (events & EV_WRITE) {
      ret |= POLLOUT;
    }

    return ret;
  }

  static short getPollEvents(uint32_t flags, short events) {
    short ret = 0;
    if (flags & POLLIN) {
      ret |= EV_READ;
    }

    if (flags & POLLOUT) {
      ret |= EV_WRITE;
    }

    if (flags & (POLLERR | POLLHUP)) {
      ret |= (EV_READ | EV_WRITE);
    }

    ret &= events;

    return ret;
  }

  // timer processing
  bool addTimerFd();
  void scheduleTimeout();
  void scheduleTimeout(const std::chrono::microseconds& us);
  void addTimerEvent(Event& event, const struct timeval* timeout);
  void removeTimerEvent(Event& event);
  size_t processTimers();
  void setProcessTimers();

  size_t processActiveEvents();

  struct IoSqe;

  static void processPollIoSqe(
      IoUringBackend* backend, IoSqe* ioSqe, int64_t res);
  static void processTimerIoSqe(
      IoUringBackend* backend, IoSqe* /*sqe*/, int64_t /*res*/);
  static void processSignalReadIoSqe(
      IoUringBackend* backend, IoSqe* /*sqe*/, int64_t /*res*/);

  // signal handling
  void addSignalEvent(Event& event);
  void removeSignalEvent(Event& event);
  bool addSignalFds();
  size_t processSignals();
  void setProcessSignals();

  void processPollIo(IoSqe* ioSqe, int64_t res) noexcept;

  IoSqe* FOLLY_NULLABLE allocIoSqe(const EventCallback& cb);
  void releaseIoSqe(IoSqe* aioIoSqe);
  void incNumIoSqeInUse() { numIoSqeInUse_++; }

  // submit immediate if POLL_SQ | POLL_SQ_IMMEDIATE_IO flags are set
  void submitImmediateIoSqe(IoSqeBase& ioSqe);

  void internalSubmit(IoSqeBase& ioSqe);

  int eb_event_modify_inserted(Event& event, IoSqe* ioSqe);

  FOLLY_ALWAYS_INLINE size_t numIoSqeInUse() const { return numIoSqeInUse_; }

  struct FdRegistry {
    FdRegistry() = delete;
    FdRegistry(struct io_uring& ioRing, size_t n);

    FdRegistrationRecord* alloc(int fd);
    bool free(FdRegistrationRecord* record);

    int init();
    size_t update();

    bool err_{false};
    struct io_uring& ioRing_;
    std::vector<int> files_;
    size_t inUse_;
    std::vector<FdRegistrationRecord> records_;
    boost::intrusive::
        slist<FdRegistrationRecord, boost::intrusive::cache_last<false>>
            free_;
  };

  struct IoSqe : public IoSqeBase {
    using BackendCb = void(IoUringBackend*, IoSqe*, int64_t);
    explicit IoSqe(
        IoUringBackend* backend = nullptr,
        bool poolAlloc = false,
        bool persist = false)
        : backend_(backend), poolAlloc_(poolAlloc), persist_(persist) {}
    virtual ~IoSqe() = default;

    void callback(int res, uint32_t) override {
      backendCb_(backend_, this, res);
    }
    void callbackCancelled() override { release(); }
    virtual void release();

    IoUringBackend* backend_;
    BackendCb* backendCb_{nullptr};
    const bool poolAlloc_;
    const bool persist_;
    Event* event_{nullptr};
    FdRegistrationRecord* fdRecord_{nullptr};
    size_t useCount_{0};

    FOLLY_ALWAYS_INLINE void resetEvent() {
      // remove it from the list
      unlink();
      if (event_) {
        event_->setUserData(nullptr);
        event_ = nullptr;
      }
    }

    void processSubmit(struct io_uring_sqe* sqe) override {
      auto* ev = event_->getEvent();
      if (ev) {
        const auto& cb = event_->getCallback();
        switch (cb.type_) {
          case EventCallback::Type::TYPE_NONE:
            break;
          case EventCallback::Type::TYPE_READ:
            if (auto* iov = cb.readCb_->allocateData()) {
              prepRead(
                  sqe,
                  ev->ev_fd,
                  &iov->data_,
                  0,
                  (ev->ev_events & EV_PERSIST) != 0);
              cbData_.set(iov);
              return;
            }
            break;
          case EventCallback::Type::TYPE_RECVMSG:
            if (auto* msg = cb.recvmsgCb_->allocateData()) {
              prepRecvmsg(
                  sqe,
                  ev->ev_fd,
                  &msg->data_,
                  (ev->ev_events & EV_PERSIST) != 0);
              cbData_.set(msg);
              return;
            }
            break;
        }

        prepPollAdd(
            sqe,
            ev->ev_fd,
            getPollFlags(ev->ev_events),
            (ev->ev_events & EV_PERSIST) != 0);
      }
    }

    virtual void processActive() {}

    struct EventCallbackData {
      EventCallback::Type type_{EventCallback::Type::TYPE_NONE};
      union {
        EventReadCallback::IoVec* ioVec_;
        EventRecvmsgCallback::MsgHdr* msgHdr_;
      };

      void set(EventReadCallback::IoVec* ioVec) {
        type_ = EventCallback::Type::TYPE_READ;
        ioVec_ = ioVec;
      }

      void set(EventRecvmsgCallback::MsgHdr* msgHdr) {
        type_ = EventCallback::Type::TYPE_RECVMSG;
        msgHdr_ = msgHdr;
      }

      void reset() { type_ = EventCallback::Type::TYPE_NONE; }

      bool processCb(int res) {
        bool ret = false;
        switch (type_) {
          case EventCallback::Type::TYPE_READ: {
            ret = true;
            auto cbFunc = ioVec_->cbFunc_;
            cbFunc(ioVec_, res);
            break;
          }
          case EventCallback::Type::TYPE_RECVMSG: {
            ret = true;
            auto cbFunc = msgHdr_->cbFunc_;
            cbFunc(msgHdr_, res);
            break;
          }
          case EventCallback::Type::TYPE_NONE:
            break;
        }
        type_ = EventCallback::Type::TYPE_NONE;

        return ret;
      }

      void releaseData() {
        switch (type_) {
          case EventCallback::Type::TYPE_READ: {
            auto freeFunc = ioVec_->freeFunc_;
            freeFunc(ioVec_);
            break;
          }
          case EventCallback::Type::TYPE_RECVMSG: {
            auto freeFunc = msgHdr_->freeFunc_;
            freeFunc(msgHdr_);
            break;
          }
          case EventCallback::Type::TYPE_NONE:
            break;
        }
        type_ = EventCallback::Type::TYPE_NONE;
      }
    };

    EventCallbackData cbData_;

    void prepPollAdd(
        struct io_uring_sqe* sqe, int fd, uint32_t events, bool registerFd) {
      CHECK(sqe);
      if (registerFd && !fdRecord_) {
        fdRecord_ = backend_->registerFd(fd);
      }

      if (fdRecord_) {
        ::io_uring_prep_poll_add(sqe, fdRecord_->idx_, events);
        sqe->flags |= IOSQE_FIXED_FILE;
      } else {
        ::io_uring_prep_poll_add(sqe, fd, events);
      }
      ::io_uring_sqe_set_data(sqe, this);
    }

    void prepRead(
        struct io_uring_sqe* sqe,
        int fd,
        const struct iovec* iov,
        off_t offset,
        bool registerFd) {
      CHECK(sqe);
      if (registerFd && !fdRecord_) {
        fdRecord_ = backend_->registerFd(fd);
      }

      if (fdRecord_) {
        ::io_uring_prep_read(
            sqe, fdRecord_->idx_, iov->iov_base, iov->iov_len, offset);
        sqe->flags |= IOSQE_FIXED_FILE;
      } else {
        ::io_uring_prep_read(sqe, fd, iov->iov_base, iov->iov_len, offset);
      }
      ::io_uring_sqe_set_data(sqe, this);
    }

    void prepWrite(
        struct io_uring_sqe* sqe,
        int fd,
        const struct iovec* iov,
        off_t offset,
        bool registerFd) {
      CHECK(sqe);
      if (registerFd && !fdRecord_) {
        fdRecord_ = backend_->registerFd(fd);
      }

      if (fdRecord_) {
        ::io_uring_prep_write(
            sqe, fdRecord_->idx_, iov->iov_base, iov->iov_len, offset);
        sqe->flags |= IOSQE_FIXED_FILE;
      } else {
        ::io_uring_prep_write(sqe, fd, iov->iov_base, iov->iov_len, offset);
      }
      ::io_uring_sqe_set_data(sqe, this);
    }

    void prepRecvmsg(
        struct io_uring_sqe* sqe, int fd, struct msghdr* msg, bool registerFd) {
      CHECK(sqe);
      if (registerFd && !fdRecord_) {
        fdRecord_ = backend_->registerFd(fd);
      }

      if (fdRecord_) {
        ::io_uring_prep_recvmsg(sqe, fdRecord_->idx_, msg, MSG_TRUNC);
        sqe->flags |= IOSQE_FIXED_FILE;
      } else {
        ::io_uring_prep_recvmsg(sqe, fd, msg, 0);
      }
      ::io_uring_sqe_set_data(sqe, this);
    }

    FOLLY_ALWAYS_INLINE void prepCancel(
        struct io_uring_sqe* sqe, IoSqe* cancel_sqe) {
      CHECK(sqe);
      ::io_uring_prep_cancel(sqe, UserData{cancel_sqe}, 0);
      ::io_uring_sqe_set_data(sqe, this);
    }
  };

  using IoSqeBaseList = boost::intrusive::
      list<IoSqeBase, boost::intrusive::constant_time_size<false>>;
  using IoSqeList = boost::intrusive::
      list<IoSqe, boost::intrusive::constant_time_size<false>>;

  struct FileOpIoSqe : public IoSqe {
    FileOpIoSqe(IoUringBackend* backend, int fd, FileOpCallback&& cb)
        : IoSqe(backend, false), fd_(fd), cb_(std::move(cb)) {}

    ~FileOpIoSqe() override = default;

    void processActive() override { cb_(res_); }

    int fd_{-1};
    int res_{-1};

    FileOpCallback cb_;
  };

  struct ReadWriteIoSqe : public FileOpIoSqe {
    ReadWriteIoSqe(
        IoUringBackend* backend,
        int fd,
        const struct iovec* iov,
        off_t offset,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, fd, std::move(cb)),
          iov_(iov, iov + 1),
          offset_(offset) {}

    ReadWriteIoSqe(
        IoUringBackend* backend,
        int fd,
        Range<const struct iovec*> iov,
        off_t offset,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, fd, std::move(cb)), iov_(iov), offset_(offset) {}

    ~ReadWriteIoSqe() override = default;

    void processActive() override { cb_(res_); }

    static constexpr size_t kNumInlineIoVec = 4;
    folly::small_vector<struct iovec> iov_;
    off_t offset_;
  };

  struct ReadIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;

    ~ReadIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      prepRead(sqe, fd_, iov_.data(), offset_, false);
    }
  };

  struct WriteIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;
    ~WriteIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      prepWrite(sqe, fd_, iov_.data(), offset_, false);
    }
  };

  struct ReadvIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;

    ~ReadvIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      ::io_uring_prep_readv(sqe, fd_, iov_.data(), iov_.size(), offset_);
      ::io_uring_sqe_set_data(sqe, this);
    }
  };

  struct WritevIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;
    ~WritevIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      ::io_uring_prep_writev(sqe, fd_, iov_.data(), iov_.size(), offset_);
      ::io_uring_sqe_set_data(sqe, this);
    }
  };

  enum class FSyncFlags {
    FLAGS_FSYNC = 0,
    FLAGS_FDATASYNC = 1,
  };

  struct FSyncIoSqe : public FileOpIoSqe {
    FSyncIoSqe(
        IoUringBackend* backend, int fd, FSyncFlags flags, FileOpCallback&& cb)
        : FileOpIoSqe(backend, fd, std::move(cb)), flags_(flags) {}

    ~FSyncIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      unsigned int fsyncFlags = 0;
      switch (flags_) {
        case FSyncFlags::FLAGS_FSYNC:
          fsyncFlags = 0;
          break;
        case FSyncFlags::FLAGS_FDATASYNC:
          fsyncFlags = IORING_FSYNC_DATASYNC;
          break;
      }

      ::io_uring_prep_fsync(sqe, fd_, fsyncFlags);
      ::io_uring_sqe_set_data(sqe, this);
    }

    FSyncFlags flags_;
  };

  struct FOpenAtIoSqe : public FileOpIoSqe {
    FOpenAtIoSqe(
        IoUringBackend* backend,
        int dfd,
        const char* path,
        int flags,
        mode_t mode,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, dfd, std::move(cb)),
          path_(path),
          flags_(flags),
          mode_(mode) {}

    ~FOpenAtIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      ::io_uring_prep_openat(sqe, fd_, path_.c_str(), flags_, mode_);
      ::io_uring_sqe_set_data(sqe, this);
    }

    std::string path_;
    int flags_;
    mode_t mode_;
  };

  struct FOpenAt2IoSqe : public FileOpIoSqe {
    FOpenAt2IoSqe(
        IoUringBackend* backend,
        int dfd,
        const char* path,
        struct open_how* how,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, dfd, std::move(cb)), path_(path), how_(*how) {}

    ~FOpenAt2IoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      ::io_uring_prep_openat2(sqe, fd_, path_.c_str(), &how_);
      ::io_uring_sqe_set_data(sqe, this);
    }

    std::string path_;
    struct open_how how_;
  };

  struct FCloseIoSqe : public FileOpIoSqe {
    using FileOpIoSqe::FileOpIoSqe;

    ~FCloseIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      ::io_uring_prep_close(sqe, fd_);
      ::io_uring_sqe_set_data(sqe, this);
    }
  };

  struct FAllocateIoSqe : public FileOpIoSqe {
    FAllocateIoSqe(
        IoUringBackend* backend,
        int fd,
        int mode,
        off_t offset,
        off_t len,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, fd, std::move(cb)),
          mode_(mode),
          offset_(offset),
          len_(len) {}

    ~FAllocateIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      ::io_uring_prep_fallocate(sqe, fd_, mode_, offset_, len_);
      ::io_uring_sqe_set_data(sqe, this);
    }

    int mode_;
    off_t offset_;
    off_t len_;
  };

  struct SendmsgIoSqe : public FileOpIoSqe {
    SendmsgIoSqe(
        IoUringBackend* backend,
        int fd,
        const struct msghdr* msg,
        unsigned int flags,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, fd, std::move(cb)), msg_(msg), flags_(flags) {}

    ~SendmsgIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      ::io_uring_prep_sendmsg(sqe, fd_, msg_, flags_);
      ::io_uring_sqe_set_data(sqe, this);
    }

    const struct msghdr* msg_;
    unsigned int flags_;
  };

  struct RecvmsgIoSqe : public FileOpIoSqe {
    RecvmsgIoSqe(
        IoUringBackend* backend,
        int fd,
        struct msghdr* msg,
        unsigned int flags,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, fd, std::move(cb)), msg_(msg), flags_(flags) {}

    ~RecvmsgIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) override {
      ::io_uring_prep_recvmsg(sqe, fd_, msg_, flags_);
      ::io_uring_sqe_set_data(sqe, this);
    }

    struct msghdr* msg_;
    unsigned int flags_;
  };

  size_t getActiveEvents(WaitForEventsMode waitForEvents);
  size_t prepList(IoSqeBaseList& ioSqes);
  int submitOne();
  int cancelOne(IoSqe* ioSqe);

  int submitBusyCheck(int num, WaitForEventsMode waitForEvents);
  int submitEager();

  void queueFsync(int fd, FSyncFlags flags, FileOpCallback&& cb);

  void processFileOp(IoSqe* ioSqe, int64_t res) noexcept;

  static void processFileOpCB(
      IoUringBackend* backend, IoSqe* ioSqe, int64_t res) {
    static_cast<IoUringBackend*>(backend)->processFileOp(ioSqe, res);
  }

  IoUringBackend::IoSqe* allocNewIoSqe(const EventCallback& /*cb*/) {
    // allow pool alloc if numIoSqeInUse_ < numEntries_
    auto* ret = new IoSqe(this, numIoSqeInUse_ < numEntries_);
    ret->backendCb_ = IoUringBackend::processPollIoSqe;

    return ret;
  }

  void cleanup();

  struct io_uring_sqe* get_sqe();

  Options options_;
  size_t numEntries_;
  std::unique_ptr<IoSqe> timerEntry_;
  std::unique_ptr<IoSqe> signalReadEntry_;
  IoSqeList freeList_;

  // timer related
  int timerFd_{-1};
  bool timerChanged_{false};
  bool timerSet_{false};
  std::multimap<std::chrono::steady_clock::time_point, Event*> timers_;

  // signal related
  SocketPair signalFds_;
  std::map<int, std::set<Event*>> signals_;

  // submit
  IoSqeBaseList submitList_;
  uint16_t bufferProviderGidNext_{0};
  std::unique_ptr<ProvidedBufferProviderBase> bufferProvider_;

  // loop related
  bool loopBreak_{false};
  bool shuttingDown_{false};
  bool processTimers_{false};
  bool processSignals_{false};
  IoSqeList activeEvents_;
  // number of IoSqe instances in use
  size_t waitingToSubmit_{0};
  size_t numInsertedEvents_{0};
  size_t numInternalEvents_{0};
  size_t numIoSqeInUse_{0};

  // io_uring related
  struct io_uring_params params_;
  struct io_uring ioRing_;

  FdRegistry fdRegistry_;

  // poll callback to be invoked if POLL_CQ flag is set
  // every time we poll for a CQE
  CQPollLoopCallback cqPollLoopCallback_;

  bool registerDefaultFds_{true};

  // stuff for ensuring we don't re-enter submit/getActiveEvents
  int isSubmitting_{0};
  bool gettingEvents_{false};
  void setSubmitting() { isSubmitting_++; }
  void doneSubmitting() { isSubmitting_--; }
  void setGetActiveEvents() {
    if (kIsDebug && gettingEvents_) {
      throw std::runtime_error("getting events is not reentrant");
      gettingEvents_ = true;
    }
  }
  void doneGetActiveEvents() { gettingEvents_ = false; }
  bool isSubmitting() const { return isSubmitting_; }
};

using PollIoBackend = IoUringBackend;
} // namespace folly

#endif // __has_include(<liburing.h>)
