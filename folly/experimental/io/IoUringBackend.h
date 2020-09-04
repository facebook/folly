/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <liburing.h>

#include <folly/Function.h>
#include <folly/Range.h>
#include <folly/experimental/io/PollIoBackend.h>
#include <folly/small_vector.h>

#include <glog/logging.h>

namespace folly {

class IoUringBackend : public PollIoBackend {
 public:
  class FOLLY_EXPORT NotAvailable : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
  };

  explicit IoUringBackend(Options options);
  ~IoUringBackend() override;

  // returns true if the current Linux kernel version
  // supports the io_uring backend
  static bool isAvailable();

  // from PollIoBackend
  FdRegistrationRecord* registerFd(int fd) override {
    return fdRegistry_.alloc(fd);
  }

  bool unregisterFd(FdRegistrationRecord* rec) override {
    return fdRegistry_.free(rec);
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

 protected:
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

  // from PollIoBackend
  void* allocSubmissionEntry() override;
  int getActiveEvents(WaitForEventsMode waitForEvents) override;
  size_t submitList(IoCbList& ioCbs, WaitForEventsMode waitForEvents) override;
  int submitOne(IoCb* ioCb) override;
  int cancelOne(IoCb* ioCb) override;

  int submitBusyCheck(int num, WaitForEventsMode waitForEvents);

  struct IoSqe : public PollIoBackend::IoCb {
    explicit IoSqe(PollIoBackend* backend = nullptr, bool poolAlloc = false)
        : PollIoBackend::IoCb(backend, poolAlloc) {}
    ~IoSqe() override = default;

    void processSubmit(void* entry) override {
      auto* ev = event_->getEvent();
      if (ev) {
        struct io_uring_sqe* sqe =
            reinterpret_cast<struct io_uring_sqe*>(entry);
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

    void prepPollAdd(void* entry, int fd, uint32_t events, bool registerFd)
        override {
      CHECK(entry);
      struct io_uring_sqe* sqe = reinterpret_cast<struct io_uring_sqe*>(entry);
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
        void* entry,
        int fd,
        const struct iovec* iov,
        off_t offset,
        bool registerFd) {
      CHECK(entry);
      struct io_uring_sqe* sqe = reinterpret_cast<struct io_uring_sqe*>(entry);
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
        void* entry,
        int fd,
        const struct iovec* iov,
        off_t offset,
        bool registerFd) {
      CHECK(entry);
      struct io_uring_sqe* sqe = reinterpret_cast<struct io_uring_sqe*>(entry);
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

    void prepRecvmsg(void* entry, int fd, struct msghdr* msg, bool registerFd) {
      CHECK(entry);
      struct io_uring_sqe* sqe = reinterpret_cast<struct io_uring_sqe*>(entry);
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
        struct io_uring_sqe* sqe,
        void* user_data) {
      CHECK(sqe);
      ::io_uring_prep_cancel(sqe, user_data, 0);
      ::io_uring_sqe_set_data(sqe, this);
    }
  };

  struct FileOpIoSqe : public IoSqe {
    FileOpIoSqe(PollIoBackend* backend, int fd, FileOpCallback&& cb)
        : IoSqe(backend, false), fd_(fd), cb_(std::move(cb)) {}

    ~FileOpIoSqe() override = default;

    void processActive() override {
      cb_(res_);
    }

    int fd_{-1};
    int res_{-1};

    FileOpCallback cb_;
  };

  struct ReadWriteIoSqe : public FileOpIoSqe {
    ReadWriteIoSqe(
        PollIoBackend* backend,
        int fd,
        const struct iovec* iov,
        off_t offset,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, fd, std::move(cb)),
          iov_(iov, iov + 1),
          offset_(offset) {}

    ReadWriteIoSqe(
        PollIoBackend* backend,
        int fd,
        Range<const struct iovec*> iov,
        off_t offset,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, fd, std::move(cb)), iov_(iov), offset_(offset) {}

    ~ReadWriteIoSqe() override = default;

    void processActive() override {
      cb_(res_);
    }

    static constexpr size_t kNumInlineIoVec = 4;
    folly::small_vector<struct iovec> iov_;
    off_t offset_;
  };

  struct ReadIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;

    ~ReadIoSqe() override = default;

    void processSubmit(void* entry) override {
      prepRead(entry, fd_, iov_.data(), offset_, false);
    }
  };

  struct WriteIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;
    ~WriteIoSqe() override = default;

    void processSubmit(void* entry) override {
      prepWrite(entry, fd_, iov_.data(), offset_, false);
    }
  };

  struct ReadvIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;

    ~ReadvIoSqe() override = default;

    void processSubmit(void* entry) override {
      struct io_uring_sqe* sqe = reinterpret_cast<struct io_uring_sqe*>(entry);
      ::io_uring_prep_readv(sqe, fd_, iov_.data(), iov_.size(), offset_);
      ::io_uring_sqe_set_data(sqe, this);
    }
  };

  struct WritevIoSqe : public ReadWriteIoSqe {
    using ReadWriteIoSqe::ReadWriteIoSqe;
    ~WritevIoSqe() override = default;

    void processSubmit(void* entry) override {
      struct io_uring_sqe* sqe = reinterpret_cast<struct io_uring_sqe*>(entry);
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
        PollIoBackend* backend,
        int fd,
        FSyncFlags flags,
        FileOpCallback&& cb)
        : FileOpIoSqe(backend, fd, std::move(cb)), flags_(flags) {}

    ~FSyncIoSqe() override = default;

    void processSubmit(void* entry) override {
      struct io_uring_sqe* sqe = reinterpret_cast<struct io_uring_sqe*>(entry);

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

  void queueFsync(int fd, FSyncFlags flags, FileOpCallback&& cb);

  void processFileOp(IoCb* ioCb, int64_t res) noexcept;

  static void processFileOpCB(PollIoBackend* backend, IoCb* ioCb, int64_t res) {
    static_cast<IoUringBackend*>(backend)->processFileOp(ioCb, res);
  }

  PollIoBackend::IoCb* allocNewIoCb(const EventCallback& /*cb*/) override {
    // allow pool alloc if numIoCbInUse_ < numEntries_
    auto* ret = new IoSqe(this, numIoCbInUse_ < numEntries_);
    ret->backendCb_ = PollIoBackend::processPollIoCb;

    return ret;
  }

  void cleanup();

  size_t submit_internal();

  // io_uring related
  struct io_uring_params params_;
  struct io_uring ioRing_;

  FdRegistry fdRegistry_;
};
} // namespace folly
