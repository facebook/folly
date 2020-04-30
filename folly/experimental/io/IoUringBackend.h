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

extern "C" {
#include <liburing.h>
}

#include <folly/experimental/io/PollIoBackend.h>
#include <glog/logging.h>

namespace folly {

class IoUringBackend : public PollIoBackend {
 public:
  class FOLLY_EXPORT NotAvailable : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
  };

  explicit IoUringBackend(
      size_t capacity,
      size_t maxSubmit = 128,
      size_t maxGet = static_cast<size_t>(-1),
      bool useRegisteredFds = false);
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
    explicit IoSqe(PollIoBackend* backend = nullptr, bool poolAlloc = true)
        : PollIoBackend::IoCb(backend, poolAlloc) {}
    ~IoSqe() override = default;

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

    void prepRead(void* entry, int fd, const struct iovec* iov, bool registerFd)
        override {
      CHECK(entry);
      struct io_uring_sqe* sqe = reinterpret_cast<struct io_uring_sqe*>(entry);
      if (registerFd && !fdRecord_) {
        fdRecord_ = backend_->registerFd(fd);
      }

      if (fdRecord_) {
        ::io_uring_prep_read(
            sqe, fdRecord_->idx_, iov->iov_base, iov->iov_len, 0 /*offset*/);
        sqe->flags |= IOSQE_FIXED_FILE;
      } else {
        ::io_uring_prep_read(
            sqe, fd, iov->iov_base, iov->iov_len, 0 /*offset*/);
      }
      ::io_uring_sqe_set_data(sqe, this);
    }

    void prepRecvmsg(void* entry, int fd, struct msghdr* msg, bool registerFd)
        override {
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

  PollIoBackend::IoCb* allocNewIoCb(const EventCallback& /*cb*/) override {
    auto* ret = new IoSqe(this, false);
    ret->backendCb_ = PollIoBackend::processPollIoCb;

    return ret;
  }

  void cleanup();

  size_t submit_internal();

  std::unique_ptr<IoSqe[]> entries_;

  // io_uring related
  struct io_uring_params params_;
  struct io_uring ioRing_;

  uint32_t sqRingMask_{0};
  uint32_t cqRingMask_{0};

  FdRegistry fdRegistry_;
};
} // namespace folly
