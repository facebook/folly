/*
 * Copyright 2013 Facebook, Inc.
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

#include "folly/experimental/io/AsyncIO.h"

#include <cerrno>

#include <boost/intrusive/parent_from_member.hpp>
#include <glog/logging.h>

#include "folly/Exception.h"
#include "folly/Likely.h"
#include "folly/String.h"
#include "folly/eventfd.h"

namespace folly {

AsyncIOOp::AsyncIOOp(NotificationCallback cb)
  : cb_(std::move(cb)),
    state_(UNINITIALIZED),
    result_(-EINVAL) {
  memset(&iocb_, 0, sizeof(iocb_));
}

void AsyncIOOp::reset(NotificationCallback cb) {
  CHECK_NE(state_, PENDING);
  cb_ = std::move(cb);
  state_ = UNINITIALIZED;
  result_ = -EINVAL;
  memset(&iocb_, 0, sizeof(iocb_));
}

AsyncIOOp::~AsyncIOOp() {
  CHECK_NE(state_, PENDING);
}

void AsyncIOOp::start() {
  DCHECK_EQ(state_, INITIALIZED);
  state_ = PENDING;
}

void AsyncIOOp::complete(ssize_t result) {
  DCHECK_EQ(state_, PENDING);
  state_ = COMPLETED;
  result_ = result;
  if (cb_) {
    cb_(this);
  }
}

ssize_t AsyncIOOp::result() const {
  CHECK_EQ(state_, COMPLETED);
  return result_;
}

void AsyncIOOp::pread(int fd, void* buf, size_t size, off_t start) {
  init();
  io_prep_pread(&iocb_, fd, buf, size, start);
}

void AsyncIOOp::pread(int fd, Range<unsigned char*> range, off_t start) {
  pread(fd, range.begin(), range.size(), start);
}

void AsyncIOOp::preadv(int fd, const iovec* iov, int iovcnt, off_t start) {
  init();
  io_prep_preadv(&iocb_, fd, iov, iovcnt, start);
}

void AsyncIOOp::pwrite(int fd, const void* buf, size_t size, off_t start) {
  init();
  io_prep_pwrite(&iocb_, fd, const_cast<void*>(buf), size, start);
}

void AsyncIOOp::pwrite(int fd, Range<const unsigned char*> range, off_t start) {
  pwrite(fd, range.begin(), range.size(), start);
}

void AsyncIOOp::pwritev(int fd, const iovec* iov, int iovcnt, off_t start) {
  init();
  io_prep_pwritev(&iocb_, fd, iov, iovcnt, start);
}

void AsyncIOOp::init() {
  CHECK_EQ(state_, UNINITIALIZED);
  state_ = INITIALIZED;
}

AsyncIO::AsyncIO(size_t capacity, PollMode pollMode)
  : ctx_(0),
    pending_(0),
    capacity_(capacity),
    pollFd_(-1) {
  CHECK_GT(capacity_, 0);
  completed_.reserve(capacity_);
  if (pollMode == POLLABLE) {
    pollFd_ = eventfd(0, EFD_NONBLOCK);
    checkUnixError(pollFd_, "AsyncIO: eventfd creation failed");
  }
}

AsyncIO::~AsyncIO() {
  CHECK_EQ(pending_, 0);
  if (ctx_) {
    int rc = io_queue_release(ctx_);
    CHECK_EQ(rc, 0) << "io_queue_release: " << errnoStr(-rc);
  }
  if (pollFd_ != -1) {
    CHECK_ERR(close(pollFd_));
  }
}

void AsyncIO::initializeContext() {
  if (!ctx_) {
    int rc = io_queue_init(capacity_, &ctx_);
    // returns negative errno
    checkKernelError(rc, "AsyncIO: io_queue_init failed");
    DCHECK(ctx_);
  }
}

void AsyncIO::submit(Op* op) {
  CHECK_EQ(op->state(), Op::INITIALIZED);
  CHECK_LT(pending_, capacity_) << "too many pending requests";
  initializeContext();  // on demand
  iocb* cb = &op->iocb_;
  cb->data = nullptr;  // unused
  if (pollFd_ != -1) {
    io_set_eventfd(cb, pollFd_);
  }
  int rc = io_submit(ctx_, 1, &cb);
  checkKernelError(rc, "AsyncIO: io_submit failed");
  DCHECK_EQ(rc, 1);
  op->start();
  ++pending_;
}

Range<AsyncIO::Op**> AsyncIO::wait(size_t minRequests) {
  CHECK(ctx_);
  CHECK_EQ(pollFd_, -1) << "wait() only allowed on non-pollable object";
  return doWait(minRequests, pending_);
}

Range<AsyncIO::Op**> AsyncIO::pollCompleted() {
  CHECK(ctx_);
  CHECK_NE(pollFd_, -1) << "pollCompleted() only allowed on pollable object";
  uint64_t numEvents;
  // This sets the eventFd counter to 0, see
  // http://www.kernel.org/doc/man-pages/online/pages/man2/eventfd.2.html
  ssize_t rc;
  do {
    rc = ::read(pollFd_, &numEvents, 8);
  } while (rc == -1 && errno == EINTR);
  if (UNLIKELY(rc == -1 && errno == EAGAIN)) {
    return Range<Op**>();  // nothing completed
  }
  checkUnixError(rc, "AsyncIO: read from event fd failed");
  DCHECK_EQ(rc, 8);

  DCHECK_GT(numEvents, 0);
  DCHECK_LE(numEvents, pending_);

  // Don't reap more than numEvents, as we've just reset the counter to 0.
  return doWait(numEvents, numEvents);
}

Range<AsyncIO::Op**> AsyncIO::doWait(size_t minRequests, size_t maxRequests) {
  io_event events[pending_];
  int count;
  do {
    // Wait forever
    count = io_getevents(ctx_, minRequests, maxRequests, events, nullptr);
  } while (count == -EINTR);
  checkKernelError(count, "AsyncIO: io_getevents failed");
  DCHECK_GE(count, minRequests);  // the man page says so
  DCHECK_LE(count, pending_);

  completed_.clear();
  if (count == 0) {
    return folly::Range<Op**>();
  }

  for (size_t i = 0; i < count; ++i) {
    DCHECK(events[i].obj);
    Op* op = boost::intrusive::get_parent_from_member(
        events[i].obj, &AsyncIOOp::iocb_);
    --pending_;
    op->complete(events[i].res);
    completed_.push_back(op);
  }

  return folly::Range<Op**>(&completed_.front(), count);
}

AsyncIOQueue::AsyncIOQueue(AsyncIO* asyncIO)
  : asyncIO_(asyncIO) {
}

AsyncIOQueue::~AsyncIOQueue() {
  CHECK_EQ(asyncIO_->pending(), 0);
}

void AsyncIOQueue::submit(AsyncIOOp* op) {
  submit([op]() { return op; });
}

void AsyncIOQueue::submit(OpFactory op) {
  queue_.push_back(op);
  maybeDequeue();
}

void AsyncIOQueue::onCompleted(AsyncIOOp* op) {
  maybeDequeue();
}

void AsyncIOQueue::maybeDequeue() {
  while (!queue_.empty() && asyncIO_->pending() < asyncIO_->capacity()) {
    auto& opFactory = queue_.front();
    auto op = opFactory();
    queue_.pop_front();

    // Interpose our completion callback
    auto& nextCb = op->notificationCallback();
    op->setNotificationCallback([this, nextCb](AsyncIOOp* op) {
      this->onCompleted(op);
      if (nextCb) nextCb(op);
    });

    asyncIO_->submit(op);
  }
}

}  // namespace folly

