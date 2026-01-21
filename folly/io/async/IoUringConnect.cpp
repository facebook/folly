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

#include <folly/io/async/IoUringConnect.h>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/IoUringBackend.h>

namespace folly {

#if FOLLY_HAS_LIBURING

IoUringConnectHandle::UniquePtr IoUringConnectHandle::create(
    EventBase* evb,
    NetworkSocket fd,
    IoUringConnectCallback* callback,
    std::chrono::milliseconds timeout) {
  auto* backend = dynamic_cast<folly::IoUringBackend*>(evb->getBackend());
  if (!backend) {
    return nullptr;
  }

  auto handle = std::make_unique<IoUringConnectHandle>(
      NotPubliclyConstructible{}, evb, backend, fd, callback);
  if (timeout.count() > 0) {
    handle->scheduleTimeout(uint32_t(timeout.count()));
  }
  backend->submitSoon(*handle);
  return handle;
}

IoUringConnectHandle::IoUringConnectHandle(
    NotPubliclyConstructible,
    EventBase* evb,
    IoUringBackend* backend,
    NetworkSocket fd,
    IoUringConnectCallback* callback)
    : IoSqeBase(IoSqeBase::Type::Connect),
      AsyncTimeout(evb),
      backend_(backend),
      fd_(fd),
      callback_(callback) {
  setEventBase(evb);
}

void IoUringConnectHandle::processSubmit(struct io_uring_sqe* sqe) noexcept {
  ::io_uring_prep_poll_add(sqe, fd_.toFd(), POLLOUT);
}

void IoUringConnectHandle::callback(
    const struct io_uring_cqe* /*cqe*/) noexcept {
  cancelTimeout();
  callback_->connectSuccess();
  return;
}

void IoUringConnectHandle::callbackCancelled(const io_uring_cqe*) noexcept {
  delete this;
}

void IoUringConnectHandle::timeoutExpired() noexcept {
  if (!cancelled()) {
    CHECK(callback_ != nullptr);
    callback_->connectTimeout();
  }
}

bool IoUringConnectHandle::cancel() {
  cancelTimeout();
  if (inFlight()) {
    backend_->cancel(this);
    return true;
  }

  return false;
}

#else

IoUringConnectHandle::UniquePtr IoUringConnectHandle::create(
    EventBase* /*evb*/,
    NetworkSocket /*fd*/,
    IoUringConnectCallback* /*callback*/,
    std::chrono::milliseconds /*timeout*/) {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

IoUringConnectHandle::IoUringConnectHandle(
    NotPubliclyConstructible,
    EventBase* evb,
    IoUringBackend* /*backend*/,
    NetworkSocket /*fd*/,
    IoUringConnectCallback* /*callback*/)
    : IoSqeBase(IoSqeBase::Type::Connect),
      AsyncTimeout(evb),
      backend_(nullptr),
      fd_(),
      callback_(nullptr) {
  (void)backend_;
  (void)fd_;
  (void)callback_;
}

void IoUringConnectHandle::processSubmit(
    struct io_uring_sqe* /*sqe*/) noexcept {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringConnectHandle::callback(
    const struct io_uring_cqe* /*cqe*/) noexcept {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringConnectHandle::callbackCancelled(const io_uring_cqe*) noexcept {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

void IoUringConnectHandle::timeoutExpired() noexcept {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

bool IoUringConnectHandle::cancel() {
  folly::terminate_with<std::runtime_error>("io_uring not supported");
}

#endif

} // namespace folly
