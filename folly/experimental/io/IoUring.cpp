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

#include <folly/experimental/io/IoUring.h>

#include <cerrno>
#include <ostream>
#include <stdexcept>
#include <string>

#include <boost/intrusive/parent_from_member.hpp>
#include <fmt/ostream.h>
#include <glog/logging.h>

#include <folly/Exception.h>
#include <folly/Likely.h>
#include <folly/String.h>
#include <folly/portability/Unistd.h>

#if __has_include(<liburing.h>)

// helpers
namespace {
// http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
uint32_t roundUpToNextPowerOfTwo(uint32_t num) {
  if (num == 0) {
    return 0;
  }
  num--;
  num |= num >> 1;
  num |= num >> 2;
  num |= num >> 4;
  num |= num >> 8;
  num |= num >> 16;
  return num + 1;
}

#define X(c) \
  case c:    \
    return #c

const char* ioUringOpToString(unsigned char op) {
  switch (op) {
    X(IORING_OP_NOP);
    X(IORING_OP_READV);
    X(IORING_OP_WRITEV);
    X(IORING_OP_FSYNC);
    X(IORING_OP_READ_FIXED);
    X(IORING_OP_WRITE_FIXED);
    X(IORING_OP_POLL_ADD);
    X(IORING_OP_POLL_REMOVE);
    X(IORING_OP_SYNC_FILE_RANGE);
    X(IORING_OP_SENDMSG);
    X(IORING_OP_RECVMSG);
    X(IORING_OP_TIMEOUT);
  };
  return "<INVALID op>";
}

#undef X

void toStream(std::ostream& os, const struct io_uring_sqe& sqe) {
  fmt::print(
      os,
      "user_data={}, opcode={}, ioprio={}, f={}, ",
      sqe.user_data,
      ioUringOpToString(sqe.opcode),
      sqe.ioprio,
      folly::AsyncBaseOp::fd2name(sqe.fd));

  switch (sqe.opcode) {
    case IORING_OP_READV:
    case IORING_OP_WRITEV: {
      auto offset = sqe.off;
      auto* iovec = reinterpret_cast<struct iovec*>(sqe.addr);
      os << "{";
      for (unsigned int i = 0; i < sqe.len; i++) {
        if (i) {
          os << ",";
        }
        fmt::print(
            os,
            "buf={}, offset={}, nbytes={}",
            iovec[i].iov_base,
            offset,
            iovec[i].iov_len);
        // advance the offset
        offset += iovec[i].iov_len;
      }
      os << "}";
    } break;
    default:
      os << "[TODO: write debug string for " << ioUringOpToString(sqe.opcode)
         << "] ";
      break;
  }
}

} // namespace

namespace folly {

IoUringOp::IoUringOp(NotificationCallback cb) : AsyncBaseOp(std::move(cb)) {}

void IoUringOp::reset(NotificationCallback cb) {
  CHECK_NE(state_, State::PENDING);
  cb_ = std::move(cb);
  state_ = State::UNINITIALIZED;
  result_ = -EINVAL;
}

IoUringOp::~IoUringOp() {}

void IoUringOp::pread(int fd, void* buf, size_t size, off_t start) {
  init();
  iov_[0].iov_base = buf;
  iov_[0].iov_len = size;
  io_uring_prep_readv(&sqe_, fd, iov_, 1, start);
  io_uring_sqe_set_data(&sqe_, this);
}

void IoUringOp::preadv(int fd, const iovec* iov, int iovcnt, off_t start) {
  init();
  io_uring_prep_readv(&sqe_, fd, iov, iovcnt, start);
  io_uring_sqe_set_data(&sqe_, this);
}

void IoUringOp::pread(
    int fd, void* buf, size_t size, off_t start, int buf_index) {
  init();
  io_uring_prep_read_fixed(&sqe_, fd, buf, size, start, buf_index);
  io_uring_sqe_set_data(&sqe_, this);
}

void IoUringOp::pwrite(int fd, const void* buf, size_t size, off_t start) {
  init();
  iov_[0].iov_base = const_cast<void*>(buf);
  iov_[0].iov_len = size;
  io_uring_prep_writev(&sqe_, fd, iov_, 1, start);
  io_uring_sqe_set_data(&sqe_, this);
}

void IoUringOp::pwritev(int fd, const iovec* iov, int iovcnt, off_t start) {
  init();
  io_uring_prep_writev(&sqe_, fd, iov, iovcnt, start);
  io_uring_sqe_set_data(&sqe_, this);
}

void IoUringOp::pwrite(
    int fd, const void* buf, size_t size, off_t start, int buf_index) {
  init();
  io_uring_prep_write_fixed(&sqe_, fd, buf, size, start, buf_index);
  io_uring_sqe_set_data(&sqe_, this);
}

void IoUringOp::toStream(std::ostream& os) const {
  os << "{" << state_ << ", ";

  if (state_ != AsyncBaseOp::State::UNINITIALIZED) {
    ::toStream(os, sqe_);
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

std::ostream& operator<<(std::ostream& os, const IoUringOp& op) {
  op.toStream(os);
  return os;
}

IoUring::IoUring(size_t capacity, PollMode pollMode, size_t maxSubmit)
    : AsyncBase(capacity, pollMode),
      maxSubmit_((maxSubmit <= capacity) ? maxSubmit : capacity) {
  ::memset(&ioRing_, 0, sizeof(ioRing_));
  ::memset(&params_, 0, sizeof(params_));

  params_.flags |= IORING_SETUP_CQSIZE;
  params_.cq_entries = roundUpToNextPowerOfTwo(capacity_);
}

IoUring::~IoUring() {
  CHECK_EQ(pending_, 0);
  if (ioRing_.ring_fd > 0) {
    ::io_uring_queue_exit(&ioRing_);
    ioRing_.ring_fd = -1;
  }
}

bool IoUring::isAvailable() {
  IoUring ioUring(1);

  try {
    ioUring.initializeContext();
  } catch (...) {
    return false;
  }

  return true;
}

int IoUring::register_buffers(
    const struct iovec* iovecs, unsigned int nr_iovecs) {
  initializeContext();

  SharedMutex::WriteHolder lk(submitMutex_);

  return io_uring_register_buffers(&ioRing_, iovecs, nr_iovecs);
}

int IoUring::unregister_buffers() {
  initializeContext();

  SharedMutex::WriteHolder lk(submitMutex_);
  return io_uring_unregister_buffers(&ioRing_);
}

void IoUring::initializeContext() {
  if (!init_.load(std::memory_order_acquire)) {
    std::lock_guard<std::mutex> lock(initMutex_);
    if (!init_.load(std::memory_order_relaxed)) {
      int rc = ::io_uring_queue_init_params(
          roundUpToNextPowerOfTwo(maxSubmit_), &ioRing_, &params_);
      checkKernelError(rc, "IoUring: io_uring_queue_init_params failed");
      DCHECK_GT(ioRing_.ring_fd, 0);
      if (pollFd_ != -1) {
        CHECK_ERR(io_uring_register_eventfd(&ioRing_, pollFd_));
      }
      init_.store(true, std::memory_order_release);
    }
  }
}

int IoUring::submitOne(AsyncBase::Op* op) {
  // -1 return here will trigger throw if op isn't an IoUringOp
  IoUringOp* iop = op->getIoUringOp();

  if (!iop) {
    return -1;
  }

  SharedMutex::WriteHolder lk(submitMutex_);
  auto* sqe = io_uring_get_sqe(&ioRing_);
  if (!sqe) {
    return -1;
  }

  *sqe = iop->getSqe();

  return io_uring_submit(&ioRing_);
}

int IoUring::submitRange(Range<AsyncBase::Op**> ops) {
  size_t num = 0;
  int total = 0;
  SharedMutex::WriteHolder lk(submitMutex_);
  for (size_t i = 0; i < ops.size(); i++) {
    IoUringOp* iop = ops[i]->getIoUringOp();
    if (!iop) {
      continue;
    }

    auto* sqe = io_uring_get_sqe(&ioRing_);
    if (!sqe) {
      break;
    }

    *sqe = iop->getSqe();
    ++num;
    if (num % maxSubmit_ == 0 || (i + 1 == ops.size())) {
      auto ret = io_uring_submit(&ioRing_);
      if (ret <= 0) {
        return total;
      }

      total += ret;
    }
  }

  return total ? total : -1;
}

Range<AsyncBase::Op**> IoUring::doWait(
    WaitType type,
    size_t minRequests,
    size_t maxRequests,
    std::vector<AsyncBase::Op*>& result) {
  result.clear();

  size_t count = 0;
  while (count < maxRequests) {
    struct io_uring_cqe* cqe = nullptr;
    if (!io_uring_peek_cqe(&ioRing_, &cqe) && cqe) {
      count++;
      Op* op = reinterpret_cast<Op*>(io_uring_cqe_get_data(cqe));
      CHECK(op);
      auto res = cqe->res;
      io_uring_cqe_seen(&ioRing_, cqe);
      decrementPending();
      switch (type) {
        case WaitType::COMPLETE:
          op->complete(res);
          break;
        case WaitType::CANCEL:
          op->cancel();
          break;
      }
      result.push_back(op);
    } else {
      if (count < minRequests) {
        io_uring_wait_cqe(&ioRing_, &cqe);
      } else {
        break;
      }
    }
  }

  return range(result);
}

} // namespace folly

#endif
