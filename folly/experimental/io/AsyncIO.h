/*
 * Copyright 2015 Facebook, Inc.
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

#ifndef FOLLY_IO_ASYNCIO_H_
#define FOLLY_IO_ASYNCIO_H_

#include <sys/types.h>
#include <sys/uio.h>
#include <libaio.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/noncopyable.hpp>

#include <folly/Portability.h>
#include <folly/Range.h>

namespace folly {

/**
 * An AsyncIOOp represents a pending operation.  You may set a notification
 * callback or you may use this class's methods directly.
 *
 * The op must remain allocated until completion.
 */
class AsyncIOOp : private boost::noncopyable {
  friend class AsyncIO;
  friend std::ostream& operator<<(std::ostream& stream, const AsyncIOOp& o);
 public:
  typedef std::function<void(AsyncIOOp*)> NotificationCallback;

  explicit AsyncIOOp(NotificationCallback cb = NotificationCallback());
  ~AsyncIOOp();

  // There would be a cancel() method here if Linux AIO actually implemented
  // it.  But let's not get your hopes up.

  enum class State {
    UNINITIALIZED,
    INITIALIZED,
    PENDING,
    COMPLETED
  };

  /**
   * Initiate a read request.
   */
  void pread(int fd, void* buf, size_t size, off_t start);
  void pread(int fd, Range<unsigned char*> range, off_t start);
  void preadv(int fd, const iovec* iov, int iovcnt, off_t start);

  /**
   * Initiate a write request.
   */
  void pwrite(int fd, const void* buf, size_t size, off_t start);
  void pwrite(int fd, Range<const unsigned char*> range, off_t start);
  void pwritev(int fd, const iovec* iov, int iovcnt, off_t start);

  /**
   * Return the current operation state.
   */
  State state() const { return state_; }

  /**
   * Reset the operation for reuse.  It is an error to call reset() on
   * an Op that is still pending.
   */
  void reset(NotificationCallback cb = NotificationCallback());

  void setNotificationCallback(NotificationCallback cb) { cb_ = std::move(cb); }
  const NotificationCallback& notificationCallback() const { return cb_; }

  /**
   * Retrieve the result of this operation.  Returns >=0 on success,
   * -errno on failure (that is, using the Linux kernel error reporting
   * conventions).  Use checkKernelError (folly/Exception.h) on the result to
   * throw a std::system_error in case of error instead.
   *
   * It is an error to call this if the Op hasn't yet started or is still
   * pending.
   */
  ssize_t result() const;

 private:
  void init();
  void start();
  void complete(ssize_t result);

  NotificationCallback cb_;
  iocb iocb_;
  State state_;
  ssize_t result_;
};

std::ostream& operator<<(std::ostream& stream, const AsyncIOOp& o);
std::ostream& operator<<(std::ostream& stream, AsyncIOOp::State state);

/**
 * C++ interface around Linux Async IO.
 */
class AsyncIO : private boost::noncopyable {
 public:
  typedef AsyncIOOp Op;

  enum PollMode {
    NOT_POLLABLE,
    POLLABLE
  };

  /**
   * Create an AsyncIO context capable of holding at most 'capacity' pending
   * requests at the same time.  As requests complete, others can be scheduled,
   * as long as this limit is not exceeded.
   *
   * Note: the maximum number of allowed concurrent requests is controlled
   * by the fs.aio-max-nr sysctl, the default value is usually 64K.
   *
   * If pollMode is POLLABLE, pollFd() will return a file descriptor that
   * can be passed to poll / epoll / select and will become readable when
   * any IOs on this AsyncIO have completed.  If you do this, you must use
   * pollCompleted() instead of wait() -- do not read from the pollFd()
   * file descriptor directly.
   *
   * You may use the same AsyncIO object from multiple threads, as long as
   * there is only one concurrent caller of wait() / pollCompleted() (perhaps
   * by always calling it from the same thread, or by providing appropriate
   * mutual exclusion)  In this case, pending() returns a snapshot
   * of the current number of pending requests.
   */
  explicit AsyncIO(size_t capacity, PollMode pollMode=NOT_POLLABLE);
  ~AsyncIO();

  /**
   * Wait for at least minRequests to complete.  Returns the requests that
   * have completed; the returned range is valid until the next call to
   * wait().  minRequests may be 0 to not block.
   */
  Range<Op**> wait(size_t minRequests);

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

 private:
  void decrementPending();
  void initializeContext();

  Range<Op**> doWait(size_t minRequests, size_t maxRequests);

  io_context_t ctx_;
  std::atomic<bool> ctxSet_;
  std::mutex initMutex_;

  std::atomic<size_t> pending_;
  std::atomic<size_t> submitted_;
  const size_t capacity_;
  int pollFd_;
  std::vector<Op*> completed_;
};

/**
 * Wrapper around AsyncIO that allows you to schedule more requests than
 * the AsyncIO's object capacity.  Other requests are queued and processed
 * in a FIFO order.
 */
class AsyncIOQueue {
 public:
  /**
   * Create a queue, using the given AsyncIO object.
   * The AsyncIO object may not be used by anything else until the
   * queue is destroyed.
   */
  explicit AsyncIOQueue(AsyncIO* asyncIO);
  ~AsyncIOQueue();

  size_t queued() const { return queue_.size(); }

  /**
   * Submit an op to the AsyncIO queue.  The op will be queued until
   * the AsyncIO object has room.
   */
  void submit(AsyncIOOp* op);

  /**
   * Submit a delayed op to the AsyncIO queue; this allows you to postpone
   * creation of the Op (which may require allocating memory, etc) until
   * the AsyncIO object has room.
   */
  typedef std::function<AsyncIOOp*()> OpFactory;
  void submit(OpFactory op);

 private:
  void onCompleted(AsyncIOOp* op);
  void maybeDequeue();

  AsyncIO* asyncIO_;

  std::deque<OpFactory> queue_;
};

}  // namespace folly

#endif /* FOLLY_IO_ASYNCIO_H_ */
