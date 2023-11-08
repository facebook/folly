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
#include <folly/ExceptionString.h>
#include <folly/Function.h>
#include <folly/Range.h>
#include <folly/experimental/io/IoUringBase.h>
#include <folly/experimental/io/Liburing.h>
#include <folly/io/IOBuf.h>
#include <folly/io/async/EventBaseBackendBase.h>
#include <folly/portability/Asm.h>
#include <folly/small_vector.h>

#if __has_include(<poll.h>)
#include <poll.h>
#endif

#if FOLLY_HAS_LIBURING

#include <liburing.h> // @manual

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

    Options& setDeferTaskRun(bool v) {
      deferTaskRun = v;

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
    bool deferTaskRun{false};

    std::chrono::milliseconds sqIdle{0};
    std::chrono::milliseconds cqIdle{0};
    std::set<uint32_t> sqCpus;
    std::string sqGroupName;
    size_t sqGroupNumThreads{1};
    size_t initalProvidedBuffersCount{0};
    size_t initalProvidedBuffersEachSize{0};
  };

  explicit IoUringBackend(Options options);
  ~IoUringBackend() override;
  Options const& options() const { return options_; }

  bool isWaitingToSubmit() const {
    return waitingToSubmit_ || !submitList_.empty();
  }
  struct io_uring* ioRingPtr() {
    return &ioRing_;
  }
  struct io_uring_params const& params() const {
    return params_;
  }

  // from EventBaseBackendBase
  int getPollableFd() const override { return ioRing_.ring_fd; }

  event_base* getEventBase() override { return nullptr; }

  int eb_event_base_loop(int flags) override;
  int eb_event_base_loopbreak() override;

  int eb_event_add(Event& event, const struct timeval* timeout) override;
  int eb_event_del(Event& event) override;

  bool eb_event_active(Event&, int) override { return false; }

  size_t loopPoll();
  void submitOutstanding();
  unsigned int processCompleted();

  // returns true if the current Linux kernel version
  // supports the io_uring backend
  static bool isAvailable();
  bool kernelHasNonBlockWriteFixes() const;
  static bool kernelSupportsRecvmsgMultishot();
  static bool kernelSupportsDeferTaskrun();
  static bool kernelSupportsSendZC();

  IoUringFdRegistrationRecord* registerFd(int fd) noexcept {
    return fdRegistry_.alloc(fd);
  }

  bool unregisterFd(IoUringFdRegistrationRecord* rec) {
    return fdRegistry_.free(rec);
  }

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

  void queueStatx(
      int dirfd,
      const char* pathname,
      int flags,
      unsigned int mask,
      struct statx* statxbuf,
      FileOpCallback&& cb);

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

  void submitNextLoop(IoSqeBase& ioSqe) noexcept;
  void submitSoon(IoSqeBase& ioSqe) noexcept;
  void submitNow(IoSqeBase& ioSqe);
  void submitNowNoCqe(IoSqeBase& ioSqe, int count = 1);
  void cancel(IoSqeBase* sqe);

  // built in buffer provider
  IoUringBufferProviderBase* bufferProvider() { return bufferProvider_.get(); }
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
    uintptr_t value;
    explicit UserData(void* p) noexcept
        : value{reinterpret_cast<uintptr_t>(p)} {}
    /* implicit */ operator uint64_t() const noexcept { return value; }
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
      IoUringBackend* backend, IoSqe* ioSqe, int res, uint32_t flags);
  static void processTimerIoSqe(
      IoUringBackend* backend,
      IoSqe* /*sqe*/,
      int /*res*/,
      uint32_t /* flags */);
  static void processSignalReadIoSqe(
      IoUringBackend* backend,
      IoSqe* /*sqe*/,
      int /*res*/,
      uint32_t /* flags */);

  // signal handling
  void addSignalEvent(Event& event);
  void removeSignalEvent(Event& event);
  bool addSignalFds();
  size_t processSignals();
  void setProcessSignals();

  void processPollIo(IoSqe* ioSqe, int res, uint32_t flags) noexcept;

  IoSqe* FOLLY_NULLABLE allocIoSqe(const EventCallback& cb);
  void releaseIoSqe(IoSqe* aioIoSqe) noexcept;

  // submit immediate if POLL_SQ | POLL_SQ_IMMEDIATE_IO flags are set
  void submitImmediateIoSqe(IoSqeBase& ioSqe);

  void internalSubmit(IoSqeBase& ioSqe) noexcept;

  enum class InternalProcessCqeMode {
    NORMAL, // process existing and any available
    AVAILABLE_ONLY, // process existing but don't get more
    CANCEL_ALL, // cancel every sqe
  };
  unsigned int internalProcessCqe(
      unsigned int maxGet, InternalProcessCqeMode mode) noexcept;

  int eb_event_modify_inserted(Event& event, IoSqe* ioSqe);

  struct FdRegistry {
    FdRegistry() = delete;
    FdRegistry(struct io_uring& ioRing, size_t n);

    IoUringFdRegistrationRecord* alloc(int fd) noexcept;
    bool free(IoUringFdRegistrationRecord* record);

    int init();
    size_t update();

    bool err_{false};
    struct io_uring& ioRing_;
    std::vector<int> files_;
    size_t inUse_;
    std::vector<IoUringFdRegistrationRecord> records_;
    boost::intrusive::
        slist<IoUringFdRegistrationRecord, boost::intrusive::cache_last<false>>
            free_;
  };

  struct IoSqe : public IoSqeBase {
    using BackendCb = void(IoUringBackend*, IoSqe*, int, uint32_t);
    explicit IoSqe(
        IoUringBackend* backend = nullptr,
        bool poolAlloc = false,
        bool persist = false)
        : backend_(backend), poolAlloc_(poolAlloc), persist_(persist) {}
    virtual ~IoSqe() override = default;

    void callback(int res, uint32_t flags) noexcept override {
      backendCb_(backend_, this, res, flags);
    }
    void callbackCancelled(int, uint32_t) noexcept override { release(); }
    virtual void release() noexcept;

    IoUringBackend* backend_;
    BackendCb* backendCb_{nullptr};
    const bool poolAlloc_;
    const bool persist_;
    Event* event_{nullptr};
    IoUringFdRegistrationRecord* fdRecord_{nullptr};
    size_t useCount_{0};
    int res_;
    uint32_t cqeFlags_;

    FOLLY_ALWAYS_INLINE void resetEvent() {
      // remove it from the list
      unlink();
      if (event_) {
        event_->setUserData(nullptr);
        event_ = nullptr;
      }
    }

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
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
          case EventCallback::Type::TYPE_RECVMSG_MULTISHOT:
            if (auto* hdr =
                    cb.recvmsgMultishotCb_->allocateRecvmsgMultishotData()) {
              prepRecvmsgMultishot(sqe, ev->ev_fd, &hdr->data_);
              cbData_.set(hdr);
              return;
            }
            break;
        }
        prepPollAdd(sqe, ev->ev_fd, getPollFlags(ev->ev_events));
      }
    }

    virtual void processActive() {}

    struct EventCallbackData {
      EventCallback::Type type_{EventCallback::Type::TYPE_NONE};
      union {
        EventReadCallback::IoVec* ioVec_;
        EventRecvmsgCallback::MsgHdr* msgHdr_;
        EventRecvmsgMultishotCallback::Hdr* hdr_;
      };

      void set(EventReadCallback::IoVec* ioVec) {
        type_ = EventCallback::Type::TYPE_READ;
        ioVec_ = ioVec;
      }

      void set(EventRecvmsgCallback::MsgHdr* msgHdr) {
        type_ = EventCallback::Type::TYPE_RECVMSG;
        msgHdr_ = msgHdr;
      }

      void set(EventRecvmsgMultishotCallback::Hdr* hdr) {
        type_ = EventCallback::Type::TYPE_RECVMSG_MULTISHOT;
        hdr_ = hdr;
      }

      void reset() { type_ = EventCallback::Type::TYPE_NONE; }

      bool processCb(IoUringBackend* backend, int res, uint32_t flags) {
        bool ret = false;
        bool released = false;
        switch (type_) {
          case EventCallback::Type::TYPE_READ: {
            released = ret = true;
            auto cbFunc = ioVec_->cbFunc_;
            cbFunc(ioVec_, res);
            break;
          }
          case EventCallback::Type::TYPE_RECVMSG: {
            released = ret = true;
            auto cbFunc = msgHdr_->cbFunc_;
            cbFunc(msgHdr_, res);
            break;
          }
          case EventCallback::Type::TYPE_RECVMSG_MULTISHOT: {
            ret = true;
            std::unique_ptr<IOBuf> buf;
            if (flags & IORING_CQE_F_BUFFER) {
              if (IoUringBufferProviderBase* bp = backend->bufferProvider()) {
                buf = bp->getIoBuf(flags >> 16, res);
              }
            }
            hdr_->cbFunc_(hdr_, res, std::move(buf));
            if (!(flags & IORING_CQE_F_MORE)) {
              hdr_->freeFunc_(hdr_);
              released = true;
            }
            break;
          }
          case EventCallback::Type::TYPE_NONE:
            break;
        }

        if (released) {
          type_ = EventCallback::Type::TYPE_NONE;
        }

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
          case EventCallback::Type::TYPE_RECVMSG_MULTISHOT:
            hdr_->freeFunc_(hdr_);
            break;
          case EventCallback::Type::TYPE_NONE:
            break;
        }
        type_ = EventCallback::Type::TYPE_NONE;
      }
    };

    EventCallbackData cbData_;

    void prepPollAdd(
        struct io_uring_sqe* sqe, int fd, uint32_t events) noexcept {
      CHECK(sqe);
      ::io_uring_prep_poll_add(sqe, fd, events);
      ::io_uring_sqe_set_data(sqe, this);
    }

    void prepRead(
        struct io_uring_sqe* sqe,
        int fd,
        const struct iovec* iov,
        off_t offset,
        bool registerFd) noexcept {
      CHECK(sqe);
      if (registerFd && !fdRecord_) {
        fdRecord_ = backend_->registerFd(fd);
      }

      if (fdRecord_) {
        ::io_uring_prep_read(
            sqe,
            fdRecord_->idx_,
            iov->iov_base,
            (unsigned int)iov->iov_len,
            offset);
        sqe->flags |= IOSQE_FIXED_FILE;
      } else {
        ::io_uring_prep_read(
            sqe, fd, iov->iov_base, (unsigned int)iov->iov_len, offset);
      }
      ::io_uring_sqe_set_data(sqe, this);
    }

    void prepWrite(
        struct io_uring_sqe* sqe,
        int fd,
        const struct iovec* iov,
        off_t offset,
        bool registerFd) noexcept {
      CHECK(sqe);
      if (registerFd && !fdRecord_) {
        fdRecord_ = backend_->registerFd(fd);
      }

      if (fdRecord_) {
        ::io_uring_prep_write(
            sqe,
            fdRecord_->idx_,
            iov->iov_base,
            (unsigned int)iov->iov_len,
            offset);
        sqe->flags |= IOSQE_FIXED_FILE;
      } else {
        ::io_uring_prep_write(
            sqe, fd, iov->iov_base, (unsigned int)iov->iov_len, offset);
      }
      ::io_uring_sqe_set_data(sqe, this);
    }

    void prepRecvmsg(
        struct io_uring_sqe* sqe,
        int fd,
        struct msghdr* msg,
        bool registerFd) noexcept {
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

    void prepRecvmsgMultishot(
        struct io_uring_sqe* sqe, int fd, struct msghdr* msg) noexcept {
      CHECK(sqe);
      ::io_uring_prep_recvmsg(sqe, fd, msg, MSG_TRUNC);
      // this magic value is set in io_uring_prep_recvmsg_multishot,
      // however this version of the library isn't available widely yet
      // so just hardcode it here
      constexpr uint16_t kMultishotFlag = 1U << 1;
      sqe->ioprio |= kMultishotFlag;
      if (IoUringBufferProviderBase* bp = backend_->bufferProvider()) {
        sqe->buf_group = bp->gid();
        sqe->flags |= IOSQE_BUFFER_SELECT;
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

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      prepRead(sqe, fd_, iov_.data(), offset_, false);
    }
  };

  struct WriteIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;
    ~WriteIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      prepWrite(sqe, fd_, iov_.data(), offset_, false);
    }
  };

  struct ReadvIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;

    ~ReadvIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      ::io_uring_prep_readv(
          sqe, fd_, iov_.data(), (unsigned int)iov_.size(), offset_);
      ::io_uring_sqe_set_data(sqe, this);
    }
  };

  struct WritevIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;
    ~WritevIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      ::io_uring_prep_writev(
          sqe, fd_, iov_.data(), (unsigned int)iov_.size(), offset_);
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

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
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

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
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

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      ::io_uring_prep_openat2(sqe, fd_, path_.c_str(), &how_);
      ::io_uring_sqe_set_data(sqe, this);
    }

    std::string path_;
    struct open_how how_;
  };

  struct FCloseIoSqe : public FileOpIoSqe {
    using FileOpIoSqe::FileOpIoSqe;

    ~FCloseIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      ::io_uring_prep_close(sqe, fd_);
      ::io_uring_sqe_set_data(sqe, this);
    }
  };

  struct FStatxIoSqe : public FileOpIoSqe {
    FStatxIoSqe(
        IoUringBackend* backend,
        int dfd,
        const char* pathname,
        int flags,
        unsigned int mask,
        struct statx* statxbuf,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, dfd, std::move(cb)),
          path_(pathname),
          flags_(flags),
          mask_(mask),
          statxbuf_(statxbuf) {}

    ~FStatxIoSqe() override = default;

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
      ::io_uring_prep_statx(sqe, fd_, path_, flags_, mask_, statxbuf_);
      ::io_uring_sqe_set_data(sqe, this);
    }

    const char* path_;
    int flags_;
    unsigned int mask_;
    struct statx* statxbuf_;
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

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
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

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
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

    void processSubmit(struct io_uring_sqe* sqe) noexcept override {
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

  int submitBusyCheck(int num, WaitForEventsMode waitForEvents) noexcept;
  int submitEager();

  void queueFsync(int fd, FSyncFlags flags, FileOpCallback&& cb);

  void processFileOp(IoSqe* ioSqe, int res) noexcept;

  static void processFileOpCB(
      IoUringBackend* backend, IoSqe* ioSqe, int res, uint32_t) {
    static_cast<IoUringBackend*>(backend)->processFileOp(ioSqe, res);
  }

  IoUringBackend::IoSqe* allocNewIoSqe(const EventCallback& /*cb*/) {
    // allow pool alloc if numPooledIoSqeInUse_ < numEntries_
    auto* ret = new IoSqe(this, numPooledIoSqeInUse_ < numEntries_);
    ++numPooledIoSqeInUse_;
    ret->backendCb_ = IoUringBackend::processPollIoSqe;

    return ret;
  }

  void cleanup();

  struct io_uring_sqe* getUntrackedSqe();
  struct io_uring_sqe* getSqe();

  /// some ring calls require being called on a single system thread, so we need
  /// to delay init of those things until the correct thread is ready
  void delayedInit();

  /// init things that are linked to the io_uring submitter concept
  /// so for DeferTaskrun, only do this in delayed init
  void initSubmissionLinked();

  Options options_;
  size_t numEntries_;
  std::unique_ptr<IoSqe> timerEntry_;
  std::unique_ptr<IoSqe> signalReadEntry_;
  IoSqeList freeList_;
  bool usingDeferTaskrun_{false};

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
  IoUringBufferProviderBase::UniquePtr bufferProvider_;

  // loop related
  bool loopBreak_{false};
  bool shuttingDown_{false};
  bool processTimers_{false};
  bool processSignals_{false};
  IoSqeList activeEvents_;
  size_t waitingToSubmit_{0};
  size_t numInsertedEvents_{0};
  size_t numInternalEvents_{0};
  size_t numSendEvents_{0};

  // number of pooled IoSqe instances in use
  size_t numPooledIoSqeInUse_{0};

  // io_uring related
  struct io_uring_params params_;
  struct io_uring ioRing_;

  FdRegistry fdRegistry_;

  // poll callback to be invoked if POLL_CQ flag is set
  // every time we poll for a CQE
  CQPollLoopCallback cqPollLoopCallback_;

  bool needsDelayedInit_{true};

  // stuff for ensuring we don't re-enter submit/getActiveEvents
  folly::Optional<std::thread::id> submitTid_;
  int isSubmitting_{0};
  bool gettingEvents_{false};
  void dCheckSubmitTid();
  void setSubmitting() noexcept { isSubmitting_++; }
  void doneSubmitting() noexcept { isSubmitting_--; }
  void setGetActiveEvents() {
    if (kIsDebug && gettingEvents_) {
      throw std::runtime_error("getting events is not reentrant");
      gettingEvents_ = true;
    }
  }
  void doneGetActiveEvents() noexcept { gettingEvents_ = false; }
  bool isSubmitting() const noexcept { return isSubmitting_; }
};

using PollIoBackend = IoUringBackend;
} // namespace folly

#endif
