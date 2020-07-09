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

#include <folly/experimental/io/IoUringBackend.h>
#include <folly/Likely.h>
#include <folly/String.h>
#include <folly/portability/Sockets.h>
#include <folly/synchronization/CallOnce.h>

#include <glog/logging.h>

namespace folly {
IoUringBackend::FdRegistry::FdRegistry(struct io_uring& ioRing, size_t n)
    : ioRing_(ioRing), files_(n, -1), inUse_(n), records_(n) {}

int IoUringBackend::FdRegistry::init() {
  if (inUse_) {
    int ret = ::io_uring_register_files(&ioRing_, files_.data(), inUse_);

    if (!ret) {
      // build and set the free list head if we succeed
      for (size_t i = 0; i < records_.size(); i++) {
        records_[i].idx_ = i;
        free_.push_front(records_[i]);
      }
    } else {
      LOG(ERROR) << "io_uring_register_files(" << inUse_ << ") "
                 << "failed errno = " << errno << ":\""
                 << folly::errnoStr(errno) << "\" " << this;
    }

    return ret;
  }

  return 0;
}

IoUringBackend::FdRegistrationRecord* IoUringBackend::FdRegistry::alloc(
    int fd) {
  if (FOLLY_UNLIKELY(err_ || free_.empty())) {
    return nullptr;
  }

  auto& record = free_.front();

  // now we have an idx
  int ret = ::io_uring_register_files_update(&ioRing_, record.idx_, &fd, 1);
  if (ret != 1) {
    // set the err_ flag so we do not retry again
    // this usually happens when we hit the file desc limit
    // and retrying this operation for every request is expensive
    err_ = true;
    LOG(ERROR) << "io_uring_register_files(1) "
               << "failed errno = " << errno << ":\"" << folly::errnoStr(errno)
               << "\" " << this;
    return nullptr;
  }

  record.fd_ = fd;
  record.count_ = 1;
  free_.pop_front();

  return &record;
}

bool IoUringBackend::FdRegistry::free(
    IoUringBackend::FdRegistrationRecord* record) {
  if (record && (--record->count_ == 0)) {
    record->fd_ = -1;
    int ret = ::io_uring_register_files_update(
        &ioRing_, record->idx_, &record->fd_, 1);

    // we add it to the free list anyway here
    free_.push_front(*record);

    return (ret == 1);
  }

  return false;
}

IoUringBackend::IoUringBackend(Options options)
    : PollIoBackend(options),
      fdRegistry_(ioRing_, options.useRegisteredFds ? options.capacity : 0) {
  ::memset(&ioRing_, 0, sizeof(ioRing_));
  ::memset(&params_, 0, sizeof(params_));

  params_.flags |= IORING_SETUP_CQSIZE;
  params_.cq_entries = options.capacity;

  // allocate entries both for poll add and cancel
  if (::io_uring_queue_init_params(
          2 * options_.maxSubmit, &ioRing_, &params_)) {
    LOG(ERROR) << "io_uring_queue_init_params(" << 2 * options_.maxSubmit << ","
               << params_.cq_entries << ") "
               << "failed errno = " << errno << ":\"" << folly::errnoStr(errno)
               << "\" " << this;
    throw NotAvailable("io_uring_queue_init error");
  }

  numEntries_ *= 2;

  // timer entry
  timerEntry_ = std::make_unique<IoSqe>(this, false);
  timerEntry_->backendCb_ = PollIoBackend::processTimerIoCb;

  // signal entry
  signalReadEntry_ = std::make_unique<IoSqe>(this, false);
  signalReadEntry_->backendCb_ = PollIoBackend::processSignalReadIoCb;

  // we need to call the init before adding the timer fd
  // so we avoid a deadlock - waiting for the queue to be drained
  if (options.useRegisteredFds) {
    // now init the file registry
    // if this fails, we still continue since we
    // can run without registered fds
    fdRegistry_.init();
  }

  // add the timer fd
  if (!addTimerFd() || !addSignalFds()) {
    cleanup();
    throw NotAvailable("io_uring_submit error");
  }
}

IoUringBackend::~IoUringBackend() {
  shuttingDown_ = true;

  cleanup();
}

void IoUringBackend::cleanup() {
  if (ioRing_.ring_fd > 0) {
    // release the nonsubmitted items from the submitList
    while (!submitList_.empty()) {
      auto* ioCb = &submitList_.front();
      submitList_.pop_front();
      releaseIoCb(ioCb);
    }

    // release the active events
    while (!activeEvents_.empty()) {
      auto* ioCb = &activeEvents_.front();
      activeEvents_.pop_front();
      releaseIoCb(ioCb);
    }

    // wait for the outstanding events to finish
    while (numIoCbInUse()) {
      struct io_uring_cqe* cqe = nullptr;
      ::io_uring_wait_cqe(&ioRing_, &cqe);
      if (cqe) {
        IoSqe* sqe = reinterpret_cast<IoSqe*>(io_uring_cqe_get_data(cqe));
        releaseIoCb(sqe);
        ::io_uring_cqe_seen(&ioRing_, cqe);
      }
    }

    // free the entries
    timerEntry_.reset();
    signalReadEntry_.reset();
    freeList_.clear_and_dispose([](auto _) { delete _; });

    // exit now
    ::io_uring_queue_exit(&ioRing_);
    ioRing_.ring_fd = -1;
  }
}

bool IoUringBackend::isAvailable() {
  static bool sAvailable = true;

  static folly::once_flag initFlag;
  folly::call_once(initFlag, [&]() {
    try {
      Options options;
      options.setCapacity(1024);
      IoUringBackend backend(options);
    } catch (const NotAvailable&) {
      sAvailable = false;
    }
  });

  return sAvailable;
}

void* IoUringBackend::allocSubmissionEntry() {
  return ::io_uring_get_sqe(&ioRing_);
}

int IoUringBackend::submitOne(IoCb* /*unused*/) {
  return submitBusyCheck(1, WaitForEventsMode::DONT_WAIT);
}

int IoUringBackend::cancelOne(IoCb* ioCb) {
  auto* rentry = static_cast<IoSqe*>(allocIoCb(EventCallback()));
  if (!rentry) {
    return 0;
  }

  auto* sqe = ::io_uring_get_sqe(&ioRing_);
  CHECK(sqe);

  rentry->prepCancel(sqe, ioCb); // prev entry

  int ret = submitBusyCheck(1, WaitForEventsMode::DONT_WAIT);

  if (ret < 0) {
    // release the sqe
    releaseIoCb(rentry);
  }

  return ret;
}

int IoUringBackend::getActiveEvents(WaitForEventsMode waitForEvents) {
  size_t i = 0;
  struct io_uring_cqe* cqe = nullptr;
  // we can be called from the submitList() method
  // or with non blocking flags
  if (FOLLY_LIKELY(waitForEvents == WaitForEventsMode::WAIT)) {
    ::io_uring_wait_cqe(&ioRing_, &cqe);
  } else {
    ::io_uring_peek_cqe(&ioRing_, &cqe);
  }
  while (cqe && (i < options_.maxGet)) {
    i++;
    IoSqe* sqe = reinterpret_cast<IoSqe*>(io_uring_cqe_get_data(cqe));
    sqe->backendCb_(this, sqe, cqe->res);
    ::io_uring_cqe_seen(&ioRing_, cqe);
    cqe = nullptr;
    ::io_uring_peek_cqe(&ioRing_, &cqe);
  }

  return static_cast<int>(i);
}

int IoUringBackend::submitBusyCheck(int num, WaitForEventsMode waitForEvents) {
  int i = 0;
  int res;
  while (i < num) {
    if (waitForEvents == WaitForEventsMode::WAIT) {
      res = ::io_uring_submit_and_wait(&ioRing_, 1);
    } else {
      res = ::io_uring_submit(&ioRing_);
    }
    if (res == -EBUSY) {
      // if we get EBUSY, try to consume some CQ entries
      getActiveEvents(WaitForEventsMode::DONT_WAIT);
      continue;
    }
    if (res < 0) {
      // continue if interrupted
      if (errno == EINTR) {
        continue;
      }

      return res;
    }

    // we do not have any other entries to submit
    if (res == 0) {
      break;
    }

    i += res;
  }

  return num;
}

size_t IoUringBackend::submitList(
    IoCbList& ioCbs,
    WaitForEventsMode waitForEvents) {
  int i = 0;
  size_t ret = 0;

  while (!ioCbs.empty()) {
    auto* entry = &ioCbs.front();
    ioCbs.pop_front();
    auto* sqe = ::io_uring_get_sqe(&ioRing_);
    CHECK(sqe); // this should not happen

    entry->processSubmit(sqe);
    i++;

    if (ioCbs.empty()) {
      int num = submitBusyCheck(i, waitForEvents);
      CHECK_EQ(num, i);
      ret += i;
    } else {
      if (static_cast<size_t>(i) == options_.maxSubmit) {
        int num = submitBusyCheck(i, WaitForEventsMode::DONT_WAIT);
        CHECK_EQ(num, i);
        ret += i;
        i = 0;
      }
    }
  }

  return ret;
}

void IoUringBackend::queueRead(
    int fd,
    void* buf,
    unsigned int nbytes,
    off_t offset,
    FileOpCallback&& cb) {
  struct iovec iov {
    buf, nbytes
  };
  auto* iocb = new ReadIoSqe(this, fd, &iov, offset, std::move(cb));
  iocb->backendCb_ = processFileOpCB;
  incNumIoCbInUse();

  submitList_.push_back(*iocb);
  numInsertedEvents_++;
}

void IoUringBackend::queueWrite(
    int fd,
    const void* buf,
    unsigned int nbytes,
    off_t offset,
    FileOpCallback&& cb) {
  struct iovec iov {
    const_cast<void*>(buf), nbytes
  };
  auto* iocb = new WriteIoSqe(this, fd, &iov, offset, std::move(cb));
  iocb->backendCb_ = processFileOpCB;
  incNumIoCbInUse();

  submitList_.push_back(*iocb);
  numInsertedEvents_++;
}

void IoUringBackend::queueReadv(
    int fd,
    Range<const struct iovec*> iovecs,
    off_t offset,
    FileOpCallback&& cb) {
  auto* iocb = new ReadvIoSqe(this, fd, iovecs, offset, std::move(cb));
  iocb->backendCb_ = processFileOpCB;
  incNumIoCbInUse();

  submitList_.push_back(*iocb);
  numInsertedEvents_++;
}

void IoUringBackend::queueWritev(
    int fd,
    Range<const struct iovec*> iovecs,
    off_t offset,
    FileOpCallback&& cb) {
  auto* iocb = new WritevIoSqe(this, fd, iovecs, offset, std::move(cb));
  iocb->backendCb_ = processFileOpCB;
  incNumIoCbInUse();

  submitList_.push_back(*iocb);
  numInsertedEvents_++;
}

void IoUringBackend::queueFsync(int fd, FileOpCallback&& cb) {
  queueFsync(fd, FSyncFlags::FLAGS_FSYNC, std::move(cb));
}

void IoUringBackend::queueFdatasync(int fd, FileOpCallback&& cb) {
  queueFsync(fd, FSyncFlags::FLAGS_FDATASYNC, std::move(cb));
}

void IoUringBackend::queueFsync(int fd, FSyncFlags flags, FileOpCallback&& cb) {
  auto* iocb = new FSyncIoSqe(this, fd, flags, std::move(cb));
  iocb->backendCb_ = processFileOpCB;
  incNumIoCbInUse();

  submitList_.push_back(*iocb);
  numInsertedEvents_++;
}

void IoUringBackend::processFileOp(IoCb* ioCb, int64_t res) noexcept {
  auto* ioSqe = reinterpret_cast<FileOpIoSqe*>(ioCb);
  // save the res
  ioSqe->res_ = res;
  activeEvents_.push_back(*ioCb);
  numInsertedEvents_--;
}

} // namespace folly
