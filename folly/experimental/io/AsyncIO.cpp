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

#include <folly/experimental/io/AsyncIO.h>

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

// debugging helpers
namespace {
#define X(c) \
  case c:    \
    return #c

const char* iocbCmdToString(short int cmd_short) {
  auto cmd = static_cast<io_iocb_cmd>(cmd_short);
  switch (cmd) {
    X(IO_CMD_PREAD);
    X(IO_CMD_PWRITE);
    X(IO_CMD_FSYNC);
    X(IO_CMD_FDSYNC);
    X(IO_CMD_POLL);
    X(IO_CMD_NOOP);
    X(IO_CMD_PREADV);
    X(IO_CMD_PWRITEV);
  };
  return "<INVALID io_iocb_cmd>";
}

#undef X

void toStream(std::ostream& os, const iocb& cb) {
  os << folly::format(
      "data={}, key={}, opcode={}, reqprio={}, fd={}, f={}, ",
      cb.data,
      cb.key,
      iocbCmdToString(cb.aio_lio_opcode),
      cb.aio_reqprio,
      cb.aio_fildes,
      folly::AsyncBaseOp::fd2name(cb.aio_fildes));

  switch (cb.aio_lio_opcode) {
    case IO_CMD_PREAD:
    case IO_CMD_PWRITE:
      os << folly::format(
          "buf={}, offset={}, nbytes={}, ",
          cb.u.c.buf,
          cb.u.c.offset,
          cb.u.c.nbytes);
      break;
    default:
      os << "[TODO: write debug string for "
         << iocbCmdToString(cb.aio_lio_opcode) << "] ";
      break;
  }
}

} // namespace

namespace folly {

AsyncIOOp::AsyncIOOp(NotificationCallback cb) : AsyncBaseOp(std::move(cb)) {
  memset(&iocb_, 0, sizeof(iocb_));
}

void AsyncIOOp::reset(NotificationCallback cb) {
  CHECK_NE(state_, State::PENDING);
  cb_ = std::move(cb);
  state_ = State::UNINITIALIZED;
  result_ = -EINVAL;
  memset(&iocb_, 0, sizeof(iocb_));
}

AsyncIOOp::~AsyncIOOp() = default;

void AsyncIOOp::pread(int fd, void* buf, size_t size, off_t start) {
  init();
  io_prep_pread(&iocb_, fd, buf, size, start);
}

void AsyncIOOp::preadv(int fd, const iovec* iov, int iovcnt, off_t start) {
  init();
  io_prep_preadv(&iocb_, fd, iov, iovcnt, start);
}

void AsyncIOOp::pwrite(int fd, const void* buf, size_t size, off_t start) {
  init();
  io_prep_pwrite(&iocb_, fd, const_cast<void*>(buf), size, start);
}

void AsyncIOOp::pwritev(int fd, const iovec* iov, int iovcnt, off_t start) {
  init();
  io_prep_pwritev(&iocb_, fd, iov, iovcnt, start);
}

void AsyncIOOp::toStream(std::ostream& os) const {
  os << "{" << state_ << ", ";

  if (state_ != AsyncBaseOp::State::UNINITIALIZED) {
    ::toStream(os, iocb_);
  }

  if (state_ == AsyncBaseOp::State::COMPLETED) {
    os << "result=" << result_;
    if (result_ < 0) {
      os << " (" << errnoStr(-result_) << ')';
    }
    os << ", ";
  }

  os << "}";
}

std::ostream& operator<<(std::ostream& os, const AsyncIOOp& op) {
  op.toStream(os);
  return os;
}

AsyncIO::AsyncIO(size_t capacity, PollMode pollMode)
    : AsyncBase(capacity, pollMode) {}

AsyncIO::~AsyncIO() {
  CHECK_EQ(pending_, 0);
  if (ctx_) {
    int rc = io_queue_release(ctx_);
    CHECK_EQ(rc, 0) << "io_queue_release: " << errnoStr(-rc);
  }
}

void AsyncIO::initializeContext() {
  if (!init_.load(std::memory_order_acquire)) {
    std::lock_guard<std::mutex> lock(initMutex_);
    if (!init_.load(std::memory_order_relaxed)) {
      int rc = io_queue_init(capacity_, &ctx_);
      // returns negative errno
      if (rc == -EAGAIN) {
        long aio_nr, aio_max;
        std::unique_ptr<FILE, int (*)(FILE*)> fp(
            fopen("/proc/sys/fs/aio-nr", "r"), fclose);
        PCHECK(fp);
        CHECK_EQ(fscanf(fp.get(), "%ld", &aio_nr), 1);

        std::unique_ptr<FILE, int (*)(FILE*)> aio_max_fp(
            fopen("/proc/sys/fs/aio-max-nr", "r"), fclose);
        PCHECK(aio_max_fp);
        CHECK_EQ(fscanf(aio_max_fp.get(), "%ld", &aio_max), 1);

        LOG(ERROR) << "No resources for requested capacity of " << capacity_;
        LOG(ERROR) << "aio_nr " << aio_nr << ", aio_max_nr " << aio_max;
      }

      checkKernelError(rc, "AsyncIO: io_queue_init failed");
      DCHECK(ctx_);
      init_.store(true, std::memory_order_release);
    }
  }
}

int AsyncIO::submitOne(AsyncBase::Op* op) {
  // -1 return here will trigger throw if op isn't an AsyncIOOp
  AsyncIOOp* aop = op->getAsyncIOOp();

  if (!aop) {
    return -1;
  }

  iocb* cb = &aop->iocb_;
  cb->data = nullptr; // unused
  if (pollFd_ != -1) {
    io_set_eventfd(cb, pollFd_);
  }

  return io_submit(ctx_, 1, &cb);
}

Range<AsyncBase::Op**> AsyncIO::doWait(
    WaitType type,
    size_t minRequests,
    size_t maxRequests,
    std::vector<AsyncBase::Op*>& result) {
  io_event events[maxRequests];

  // Unfortunately, Linux AIO doesn't implement io_cancel, so even for
  // WaitType::CANCEL we have to wait for IO completion.
  size_t count = 0;
  do {
    int ret;
    do {
      // GOTCHA: io_getevents() may returns less than min_nr results if
      // interrupted after some events have been read (if before, -EINTR
      // is returned).
      ret = io_getevents(
          ctx_,
          minRequests - count,
          maxRequests - count,
          events + count,
          /* timeout */ nullptr); // wait forever
    } while (ret == -EINTR);
    // Check as may not be able to recover without leaking events.
    CHECK_GE(ret, 0) << "AsyncIO: io_getevents failed with error "
                     << errnoStr(-ret);
    count += ret;
  } while (count < minRequests);
  DCHECK_LE(count, maxRequests);

  result.clear();
  for (size_t i = 0; i < count; ++i) {
    CHECK(events[i].obj);
    Op* op = boost::intrusive::get_parent_from_member(
        events[i].obj, &AsyncIOOp::iocb_);
    decrementPending();
    switch (type) {
      case WaitType::COMPLETE:
        complete(op, events[i].res);
        break;
      case WaitType::CANCEL:
        cancel(op);
        break;
    }
    result.push_back(op);
  }

  return range(result);
}

} // namespace folly
