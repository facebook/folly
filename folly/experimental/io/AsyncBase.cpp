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

#include <folly/experimental/io/AsyncBase.h>

#include <sys/eventfd.h>
#include <cerrno>
#include <ostream>
#include <stdexcept>
#include <string>

#include <boost/intrusive/parent_from_member.hpp>
#include <glog/logging.h>

#include <folly/Exception.h>
#include <folly/Format.h>
#include <folly/Likely.h>
#include <folly/String.h>
#include <folly/portability/Unistd.h>

namespace folly {

AsyncBaseOp::AsyncBaseOp(NotificationCallback cb)
    : cb_(std::move(cb)), state_(State::UNINITIALIZED), result_(-EINVAL) {}

void AsyncBaseOp::reset(NotificationCallback cb) {
  CHECK_NE(state_, State::PENDING);
  cb_ = std::move(cb);
  state_ = State::UNINITIALIZED;
  result_ = -EINVAL;
}

AsyncBaseOp::~AsyncBaseOp() {
  CHECK_NE(state_, State::PENDING);
}

void AsyncBaseOp::start() {
  DCHECK_EQ(state_, State::INITIALIZED);
  state_ = State::PENDING;
}

void AsyncBaseOp::complete(ssize_t result) {
  DCHECK_EQ(state_, State::PENDING);
  state_ = State::COMPLETED;
  result_ = result;
  if (cb_) {
    cb_(this);
  }
}

void AsyncBaseOp::cancel() {
  DCHECK_EQ(state_, State::PENDING);
  state_ = State::CANCELED;
}

ssize_t AsyncBaseOp::result() const {
  CHECK_EQ(state_, State::COMPLETED);
  return result_;
}

void AsyncBaseOp::init() {
  CHECK_EQ(state_, State::UNINITIALIZED);
  state_ = State::INITIALIZED;
}

std::string AsyncBaseOp::fd2name(int fd) {
  std::string path = folly::to<std::string>("/proc/self/fd/", fd);
  char link[PATH_MAX];
  const ssize_t length =
      std::max<ssize_t>(readlink(path.c_str(), link, PATH_MAX), 0);
  return path.assign(link, length);
}

AsyncBase::AsyncBase(size_t capacity, PollMode pollMode) : capacity_(capacity) {
  CHECK_GT(capacity_, 0);
  completed_.reserve(capacity_);
  if (pollMode == POLLABLE) {
    pollFd_ = eventfd(0, EFD_NONBLOCK);
    checkUnixError(pollFd_, "AsyncBase: eventfd creation failed");
  }
}

AsyncBase::~AsyncBase() {
  CHECK_EQ(pending_, 0);
  if (pollFd_ != -1) {
    CHECK_ERR(close(pollFd_));
  }
}

void AsyncBase::decrementPending() {
  auto p =
      pending_.fetch_add(static_cast<size_t>(-1), std::memory_order_acq_rel);
  DCHECK_GE(p, 1);
}

void AsyncBase::submit(Op* op) {
  CHECK_EQ(op->state(), Op::State::INITIALIZED);
  initializeContext(); // on demand

  // We can increment past capacity, but we'll clean up after ourselves.
  auto p = pending_.fetch_add(1, std::memory_order_acq_rel);
  if (p >= capacity_) {
    decrementPending();
    throw std::range_error("AsyncBase: too many pending requests");
  }

  int rc = submitOne(op);

  if (rc < 0) {
    decrementPending();
    throwSystemErrorExplicit(-rc, "AsyncBase: io_submit failed");
  }
  submitted_++;
  DCHECK_EQ(rc, 1);
  op->start();
}

Range<AsyncBase::Op**> AsyncBase::wait(size_t minRequests) {
  CHECK(isInit());
  CHECK_EQ(pollFd_, -1) << "wait() only allowed on non-pollable object";
  auto p = pending_.load(std::memory_order_acquire);
  CHECK_LE(minRequests, p);
  return doWait(WaitType::COMPLETE, minRequests, p, completed_);
}

Range<AsyncBase::Op**> AsyncBase::cancel() {
  CHECK(isInit());
  auto p = pending_.load(std::memory_order_acquire);
  return doWait(WaitType::CANCEL, p, p, canceled_);
}

Range<AsyncBase::Op**> AsyncBase::pollCompleted() {
  CHECK(isInit());
  CHECK_NE(pollFd_, -1) << "pollCompleted() only allowed on pollable object";
  uint64_t numEvents;
  // This sets the eventFd counter to 0, see
  // http://www.kernel.org/doc/man-pages/online/pages/man2/eventfd.2.html
  ssize_t rc;
  do {
    rc = ::read(pollFd_, &numEvents, 8);
  } while (rc == -1 && errno == EINTR);
  if (UNLIKELY(rc == -1 && errno == EAGAIN)) {
    return Range<Op**>(); // nothing completed
  }
  checkUnixError(rc, "AsyncBase: read from event fd failed");
  DCHECK_EQ(rc, 8);

  DCHECK_GT(numEvents, 0);
  DCHECK_LE(numEvents, pending_);

  // Don't reap more than numEvents, as we've just reset the counter to 0.
  return doWait(WaitType::COMPLETE, numEvents, numEvents, completed_);
}

AsyncBaseQueue::AsyncBaseQueue(AsyncBase* asyncBase) : asyncBase_(asyncBase) {}

AsyncBaseQueue::~AsyncBaseQueue() {
  CHECK_EQ(asyncBase_->pending(), 0);
}

void AsyncBaseQueue::submit(AsyncBaseOp* op) {
  submit([op]() { return op; });
}

void AsyncBaseQueue::submit(OpFactory op) {
  queue_.push_back(op);
  maybeDequeue();
}

void AsyncBaseQueue::onCompleted(AsyncBaseOp* /* op */) {
  maybeDequeue();
}

void AsyncBaseQueue::maybeDequeue() {
  while (!queue_.empty() && asyncBase_->pending() < asyncBase_->capacity()) {
    auto& opFactory = queue_.front();
    auto op = opFactory();
    queue_.pop_front();

    // Interpose our completion callback
    auto& nextCb = op->notificationCallback();
    op->setNotificationCallback([this, nextCb](AsyncBaseOp* op2) {
      this->onCompleted(op2);
      if (nextCb) {
        nextCb(op2);
      }
    });

    asyncBase_->submit(op);
  }
}

// debugging helpers:

namespace {

#define X(c) \
  case c:    \
    return #c

const char* asyncIoOpStateToString(AsyncBaseOp::State state) {
  switch (state) {
    X(AsyncBaseOp::State::UNINITIALIZED);
    X(AsyncBaseOp::State::INITIALIZED);
    X(AsyncBaseOp::State::PENDING);
    X(AsyncBaseOp::State::COMPLETED);
    X(AsyncBaseOp::State::CANCELED);
  }
  return "<INVALID AsyncBaseOp::State>";
}
#undef X
} // namespace

std::ostream& operator<<(std::ostream& os, const AsyncBaseOp& op) {
  op.toStream(os);
  return os;
}

std::ostream& operator<<(std::ostream& os, AsyncBaseOp::State state) {
  return os << asyncIoOpStateToString(state);
}

} // namespace folly
