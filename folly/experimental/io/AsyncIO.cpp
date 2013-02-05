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

#include <glog/logging.h>

#include "folly/Exception.h"
#include "folly/Likely.h"
#include "folly/String.h"
#include "folly/eventfd.h"

namespace folly {

AsyncIO::AsyncIO(size_t capacity, PollMode pollMode)
  : ctx_(0),
    pending_(0),
    capacity_(capacity),
    pollFd_(-1) {
  if (UNLIKELY(capacity_ == 0)) {
    throw std::out_of_range("AsyncIO: capacity must not be 0");
  }
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

void AsyncIO::pread(Op* op, int fd, void* buf, size_t size, off_t start) {
  iocb cb;
  io_prep_pread(&cb, fd, buf, size, start);
  submit(op, &cb);
}

void AsyncIO::pread(Op* op, int fd, Range<unsigned char*> range,
                    off_t start) {
  pread(op, fd, range.begin(), range.size(), start);
}

void AsyncIO::preadv(Op* op, int fd, const iovec* iov, int iovcnt,
                     off_t start) {
  iocb cb;
  io_prep_preadv(&cb, fd, iov, iovcnt, start);
  submit(op, &cb);
}

void AsyncIO::pwrite(Op* op, int fd, const void* buf, size_t size,
                     off_t start) {
  iocb cb;
  io_prep_pwrite(&cb, fd, const_cast<void*>(buf), size, start);
  submit(op, &cb);
}

void AsyncIO::pwrite(Op* op, int fd, Range<const unsigned char*> range,
                     off_t start) {
  pwrite(op, fd, range.begin(), range.size(), start);
}

void AsyncIO::pwritev(Op* op, int fd, const iovec* iov, int iovcnt,
                      off_t start) {
  iocb cb;
  io_prep_pwritev(&cb, fd, iov, iovcnt, start);
  submit(op, &cb);
}

void AsyncIO::initializeContext() {
  if (!ctx_) {
    int rc = io_queue_init(capacity_, &ctx_);
    // returns negative errno
    checkKernelError(rc, "AsyncIO: io_queue_init failed");
    DCHECK(ctx_);
  }
}

void AsyncIO::submit(Op* op, iocb* cb) {
  if (UNLIKELY(pending_ >= capacity_)) {
    throw std::out_of_range("AsyncIO: too many pending requests");
  }
  if (UNLIKELY(op->state() != Op::UNINITIALIZED)) {
    throw std::logic_error("AsyncIO: Invalid Op state in submit");
  }
  initializeContext();  // on demand
  cb->data = op;
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
  if (UNLIKELY(!ctx_)) {
    throw std::logic_error("AsyncIO: wait called with no requests");
  }
  if (UNLIKELY(pollFd_ != -1)) {
    throw std::logic_error("AsyncIO: wait not allowed on pollable object");
  }
  return doWait(minRequests, pending_);
}

Range<AsyncIO::Op**> AsyncIO::pollCompleted() {
  if (UNLIKELY(!ctx_)) {
    throw std::logic_error("AsyncIO: pollCompleted called with no requests");
  }
  if (UNLIKELY(pollFd_ == -1)) {
    throw std::logic_error(
        "AsyncIO: pollCompleted not allowed on non-pollable object");
  }
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
    Op* op = static_cast<Op*>(events[i].data);
    DCHECK(op);
    op->complete(events[i].res);
    completed_.push_back(op);
  }
  pending_ -= count;

  return folly::Range<Op**>(&completed_.front(), count);
}

AsyncIO::Op::Op()
  : state_(UNINITIALIZED),
    result_(-EINVAL) {
}

void AsyncIO::Op::reset() {
  if (UNLIKELY(state_ == PENDING)) {
    throw std::logic_error("AsyncIO: invalid state for reset");
  }
  state_ = UNINITIALIZED;
  result_ = -EINVAL;
}

AsyncIO::Op::~Op() {
  CHECK_NE(state_, PENDING);
}

void AsyncIO::Op::start() {
  DCHECK_EQ(state_, UNINITIALIZED);
  state_ = PENDING;
}

void AsyncIO::Op::complete(ssize_t result) {
  DCHECK_EQ(state_, PENDING);
  state_ = COMPLETED;
  result_ = result;
  onCompleted();
}

void AsyncIO::Op::onCompleted() { }  // default: do nothing

ssize_t AsyncIO::Op::result() const {
  if (UNLIKELY(state_ != COMPLETED)) {
    throw std::logic_error("AsyncIO: Invalid Op state in result");
  }
  return result_;
}

CallbackOp::CallbackOp(Callback&& callback) : callback_(std::move(callback)) { }

CallbackOp::~CallbackOp() { }

CallbackOp* CallbackOp::make(Callback&& callback) {
  // Ensure created on the heap
  return new CallbackOp(std::move(callback));
}

void CallbackOp::onCompleted() {
  callback_(result());
  delete this;
}

}  // namespace folly

