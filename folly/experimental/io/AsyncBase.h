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

#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <utility>
#include <vector>

#include <folly/Function.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/portability/SysUio.h>

namespace folly {
class AsyncIOOp;
class IoUringOp;
/**
 * An AsyncBaseOp represents a pending operation.  You may set a notification
 * callback or you may use this class's methods directly.
 *
 * The op must remain allocated until it is completed or canceled.
 */
class AsyncBaseOp {
  friend class AsyncBase;

 public:
  using NotificationCallback = folly::Function<void(AsyncBaseOp*)>;

  explicit AsyncBaseOp(NotificationCallback cb = NotificationCallback());
  AsyncBaseOp(const AsyncBaseOp&) = delete;
  AsyncBaseOp& operator=(const AsyncBaseOp&) = delete;
  virtual ~AsyncBaseOp();

  enum class State {
    UNINITIALIZED,
    INITIALIZED,
    PENDING,
    COMPLETED,
    CANCELED,
  };

  /**
   * Initiate a read request.
   */
  virtual void pread(int fd, void* buf, size_t size, off_t start) = 0;
  void pread(int fd, Range<unsigned char*> range, off_t start) {
    pread(fd, range.begin(), range.size(), start);
  }
  virtual void preadv(int fd, const iovec* iov, int iovcnt, off_t start) = 0;
  virtual void pread(
      int fd, void* buf, size_t size, off_t start, int /*buf_index*/) {
    pread(fd, buf, size, start);
  }

  /**
   * Initiate a write request.
   */
  virtual void pwrite(int fd, const void* buf, size_t size, off_t start) = 0;
  void pwrite(int fd, Range<const unsigned char*> range, off_t start) {
    pwrite(fd, range.begin(), range.size(), start);
  }
  virtual void pwritev(int fd, const iovec* iov, int iovcnt, off_t start) = 0;
  virtual void pwrite(
      int fd, const void* buf, size_t size, off_t start, int /*buf_index*/) {
    pwrite(fd, buf, size, start);
  }

  // we support only these subclasses
  virtual AsyncIOOp* getAsyncIOOp() = 0;
  virtual IoUringOp* getIoUringOp() = 0;

  // ostream output
  virtual void toStream(std::ostream& os) const = 0;

  /**
   * Return the current operation state.
   */
  State state() const { return state_; }

  /**
   * user data get/set
   */
  void* getUserData() const { return userData_; }

  void setUserData(void* userData) { userData_ = userData; }

  /**
   * Reset the operation for reuse.  It is an error to call reset() on
   * an Op that is still pending.
   */
  virtual void reset(NotificationCallback cb = NotificationCallback()) = 0;

  void setNotificationCallback(NotificationCallback cb) { cb_ = std::move(cb); }

  /**
   * Get the notification callback from the op.
   *
   * Note that this moves the callback out, leaving the callback in an
   * uninitialized state! You must call setNotificationCallback before
   * submitting the operation!
   */
  NotificationCallback getNotificationCallback() { return std::move(cb_); }

  /**
   * Retrieve the result of this operation.  Returns >=0 on success,
   * -errno on failure (that is, using the Linux kernel error reporting
   * conventions).  Use checkKernelError (folly/Exception.h) on the result to
   * throw a std::system_error in case of error instead.
   *
   * It is an error to call this if the Op hasn't completed.
   */
  ssize_t result() const;

  // debug helper
  static std::string fd2name(int fd);

 protected:
  void init();
  void start();
  void unstart();
  void complete(ssize_t result);
  void cancel();

  NotificationCallback cb_;
  State state_;
  ssize_t result_;
  void* userData_{nullptr};
};

std::ostream& operator<<(std::ostream& os, const AsyncBaseOp& op);
std::ostream& operator<<(std::ostream& os, AsyncBaseOp::State state);

/**
 * Generic C++ interface around Linux IO(io_submit, io_uring)
 */
class AsyncBase {
 public:
  using Op = AsyncBaseOp;

  enum PollMode {
    NOT_POLLABLE,
    POLLABLE,
  };

  /**
   * Create an AsyncBase context capable of holding at most 'capacity' pending
   * requests at the same time.  As requests complete, others can be scheduled,
   * as long as this limit is not exceeded.
   *
   * If pollMode is POLLABLE, pollFd() will return a file descriptor that
   * can be passed to poll / epoll / select and will become readable when
   * any IOs on this AsyncBase have completed.  If you do this, you must use
   * pollCompleted() instead of wait() -- do not read from the pollFd()
   * file descriptor directly.
   *
   * You may use the same AsyncBase object from multiple threads, as long as
   * there is only one concurrent caller of wait() / pollCompleted() / cancel()
   * (perhaps by always calling it from the same thread, or by providing
   * appropriate mutual exclusion).  In this case, pending() returns a snapshot
   * of the current number of pending requests.
   */
  explicit AsyncBase(size_t capacity, PollMode pollMode = NOT_POLLABLE);
  AsyncBase(const AsyncBase&) = delete;
  AsyncBase& operator=(const AsyncBase&) = delete;
  virtual ~AsyncBase();

  /**
   * Initialize context
   */
  virtual void initializeContext() = 0;

  /**
   * Wait for at least minRequests to complete.  Returns the requests that
   * have completed; the returned range is valid until the next call to
   * wait().  minRequests may be 0 to not block.
   */
  Range<Op**> wait(size_t minRequests);

  /**
   * Cancel all pending requests and return them; the returned range is
   * valid until the next call to cancel().
   */
  Range<Op**> cancel();

  /**
   * Return the number of pending requests.
   */
  size_t pending() const { return pending_; }

  /**
   * Return the maximum number of requests that can be kept outstanding
   * at any one time.
   */
  size_t capacity() const { return capacity_; }

  /**
   * Return the accumulative number of submitted I/O, since this object
   * has been created.
   */
  size_t totalSubmits() const { return submitted_; }

  /**
   * If POLLABLE, return a file descriptor that can be passed to poll / epoll
   * and will become readable when any async IO operations have completed.
   * If NOT_POLLABLE, return -1.
   */
  int pollFd() const { return pollFd_; }

  /**
   * If POLLABLE, call instead of wait after the file descriptor returned
   * by pollFd() became readable.  The returned range is valid until the next
   * call to pollCompleted().
   */
  Range<Op**> pollCompleted();

  /**
   * Submit an op for execution.
   */
  void submit(Op* op);

  /**
   * Submit a range of ops for execution
   */
  int submit(Range<Op**> ops);

 protected:
  void complete(Op* op, ssize_t result) { op->complete(result); }

  void cancel(Op* op) { op->cancel(); }

  bool isInit() const { return init_.load(std::memory_order_relaxed); }

  void decrementPending(size_t num = 1);
  virtual int submitOne(AsyncBase::Op* op) = 0;
  virtual int submitRange(Range<AsyncBase::Op**> ops) = 0;

  enum class WaitType { COMPLETE, CANCEL };
  virtual Range<AsyncBase::Op**> doWait(
      WaitType type,
      size_t minRequests,
      size_t maxRequests,
      std::vector<Op*>& result) = 0;

  std::atomic<bool> init_{false};
  std::mutex initMutex_;

  std::atomic<size_t> pending_{0};
  std::atomic<size_t> submitted_{0};
  const size_t capacity_;
  int pollFd_{-1};
  std::vector<Op*> completed_;
  std::vector<Op*> canceled_;
};

/**
 * Wrapper around AsyncBase that allows you to schedule more requests than
 * the AsyncBase's object capacity.  Other requests are queued and processed
 * in a FIFO order.
 */
class AsyncBaseQueue {
 public:
  /**
   * Create a queue, using the given AsyncBase object.
   * The AsyncBase object may not be used by anything else until the
   * queue is destroyed.
   */
  explicit AsyncBaseQueue(AsyncBase* asyncBase);
  ~AsyncBaseQueue();

  size_t queued() const { return queue_.size(); }

  /**
   * Submit an op to the AsyncBase queue.  The op will be queued until
   * the AsyncBase object has room.
   */
  void submit(AsyncBaseOp* op);

  /**
   * Submit a delayed op to the AsyncBase queue; this allows you to postpone
   * creation of the Op (which may require allocating memory, etc) until
   * the AsyncBase object has room.
   */
  using OpFactory = std::function<AsyncBaseOp*()>;
  void submit(OpFactory op);

 private:
  void onCompleted(AsyncBaseOp* op);
  void maybeDequeue();

  AsyncBase* asyncBase_;

  std::deque<OpFactory> queue_;
};

} // namespace folly
