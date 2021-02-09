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

#include <folly/experimental/io/SimpleAsyncIO.h>

#include <folly/String.h>
#if __has_include(<folly/experimental/io/AsyncIO.h>) && __has_include(<libaio.h>)
#define AIO_SUPPORTED
#include <folly/experimental/io/AsyncIO.h>
#endif
#if __has_include(<folly/experimental/io/IoUring.h>) && __has_include(<liburing.h>)
#define IOURING_SUPPORTED
#include <folly/experimental/io/IoUring.h>
#endif

#if !defined(AIO_SUPPORTED) && !defined(IOURING_SUPPORTED)
#error "Cannot build without at least one of AsyncIO.h and IoUring.h"
#endif

#include <folly/experimental/coro/Baton.h>
#include <folly/portability/Sockets.h>

namespace folly {

SimpleAsyncIO::SimpleAsyncIO(Config cfg)
    : maxRequests_(cfg.maxRequests_),
      completionExecutor_(cfg.completionExecutor_),
      terminating_(false) {
#if !defined(AIO_SUPPORTED)
  if (cfg.mode_ == AIO) {
    LOG(WARNING)
        << "aio mode requested but not available at link time, falling back to io_uring.";
    cfg.setMode(IOURING);
  }
#endif
#if !defined(IOURING_SUPPORTED)
  if (cfg.mode_ == IOURING) {
    LOG(WARNING)
        << "io_uring mode requested but not available at link time, falling back to aio.";
    cfg.setMode(AIO);
  }
#else
  if (cfg.mode_ == IOURING && !IoUring::isAvailable()) {
#if defined(AIO_SUPPORTED)
    LOG(WARNING)
        << "io_uring requested but not available at runtime, falling back to aio mode.";
    cfg.setMode(AIO);
#else
    LOG(FATAL)
        << "io_uring requested but not available at runtime, aio not available at link time, cannot proceed.";
#endif
  }
#endif
  switch (cfg.mode_) {
#if defined(AIO_SUPPORTED)
    case AIO:
      asyncIO_ = std::make_unique<AsyncIO>(maxRequests_, AsyncBase::POLLABLE);
      opsFreeList_.withWLock(
          [this](std::queue<std::unique_ptr<AsyncBaseOp>>& freeList) {
            for (size_t i = 0; i < maxRequests_; ++i) {
              freeList.push(std::make_unique<AsyncIOOp>());
            }
          });
      break;
#endif
#ifdef IOURING_SUPPORTED
    case IOURING:
      asyncIO_ = std::make_unique<IoUring>(maxRequests_, AsyncBase::POLLABLE);
      opsFreeList_.withWLock(
          [this](std::queue<std::unique_ptr<AsyncBaseOp>>& freeList) {
            for (size_t i = 0; i < maxRequests_; ++i) {
              freeList.push(std::make_unique<IoUringOp>());
            }
          });
      break;
#endif
    default:
      // Should never happen...
      LOG(FATAL) << "Unavailable mode " << (int)cfg.mode_ << " requested.";
      break;
  }

  if (cfg.evb_) {
    initHandler(cfg.evb_, NetworkSocket::fromFd(asyncIO_->pollFd()));
  } else {
    evb_ = std::make_unique<ScopedEventBaseThread>();
    initHandler(
        evb_->getEventBase(), NetworkSocket::fromFd(asyncIO_->pollFd()));
  }
  registerHandler(EventHandler::READ | EventHandler::PERSIST);
}

SimpleAsyncIO::~SimpleAsyncIO() {
  // stop accepting new IO.
  opsFreeList_.withWLock(
      [this](std::queue<std::unique_ptr<AsyncBaseOp>>& freeList) mutable {
        terminating_ = true;
        if (freeList.size() == maxRequests_) {
          drainedBaton_.post();
        }
      });

  drainedBaton_.wait();

  unregisterHandler();
}

void SimpleAsyncIO::handlerReady(uint16_t events) noexcept {
  if (events & EventHandler::READ) {
    // All the work (including putting op back on free list) happens in the
    // notificationCallback, so we can simply drop the ops returned from
    // pollCompleted. But we must still call it or ops never complete.
    while (asyncIO_->pollCompleted().size()) {
      ;
    }
  }
}

std::unique_ptr<AsyncBaseOp> SimpleAsyncIO::getOp() {
  std::unique_ptr<AsyncBaseOp> rc;
  opsFreeList_.withWLock(
      [this, &rc](std::queue<std::unique_ptr<AsyncBaseOp>>& freeList) {
        if (!freeList.empty() && !terminating_) {
          rc = std::move(freeList.front());
          freeList.pop();
          rc->reset();
        }
      });
  return rc;
}

void SimpleAsyncIO::putOp(std::unique_ptr<AsyncBaseOp>&& op) {
  opsFreeList_.withWLock(
      [this, op{std::move(op)}](
          std::queue<std::unique_ptr<AsyncBaseOp>>& freeList) mutable {
        freeList.push(std::move(op));
        if (terminating_ && freeList.size() == maxRequests_) {
          drainedBaton_.post();
        }
      });
}

void SimpleAsyncIO::submitOp(
    Function<void(AsyncBaseOp*)> preparer, SimpleAsyncIOCompletor completor) {
  std::unique_ptr<AsyncBaseOp> opHolder = getOp();
  if (!opHolder) {
    completor(-EBUSY);
    return;
  }

  // Grab a raw pointer to the op before we create the completion lambda,
  // since we move the unique_ptr into the lambda and can no longer access
  // it.
  AsyncBaseOp* op = opHolder.get();

  preparer(op);

  op->setNotificationCallback(
      [this, completor{std::move(completor)}, opHolder{std::move(opHolder)}](
          AsyncBaseOp* op) mutable {
        CHECK(op == opHolder.get());
        int rc = op->result();

        completionExecutor_->add(
            [rc, completor{std::move(completor)}]() mutable { completor(rc); });

        // NB: the moment we put the opHolder, the destructor might delete the
        // current instance. So do not access any member variables after this
        // point! Also, obviously, do not access op.
        putOp(std::move(opHolder));
      });
  asyncIO_->submit(op);
}

void SimpleAsyncIO::pread(
    int fd,
    void* buf,
    size_t size,
    off_t start,
    SimpleAsyncIOCompletor completor) {
  submitOp(
      [=](AsyncBaseOp* op) { op->pread(fd, buf, size, start); },
      std::move(completor));
}

void SimpleAsyncIO::pwrite(
    int fd,
    const void* buf,
    size_t size,
    off_t start,
    SimpleAsyncIOCompletor completor) {
  submitOp(
      [=](AsyncBaseOp* op) { op->pwrite(fd, buf, size, start); },
      std::move(completor));
}

#if FOLLY_HAS_COROUTINES
folly::coro::Task<int> SimpleAsyncIO::co_pwrite(
    int fd, const void* buf, size_t size, off_t start) {
  folly::coro::Baton done;
  int result;
  pwrite(fd, buf, size, start, [&done, &result](int rc) {
    result = rc;
    done.post();
  });
  co_await done;
  co_return result;
}

folly::coro::Task<int> SimpleAsyncIO::co_pread(
    int fd, void* buf, size_t size, off_t start) {
  folly::coro::Baton done;
  int result;
  pread(fd, buf, size, start, [&done, &result](int rc) {
    result = rc;
    done.post();
  });
  co_await done;
  co_return result;
}
#endif // FOLLY_HAS_COROUTINES

} // namespace folly
