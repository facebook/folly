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

#ifndef FOLLY_IO_ASYNCIO_H_
#define FOLLY_IO_ASYNCIO_H_

#include <sys/types.h>
#include <sys/uio.h>
#include <libaio.h>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <boost/noncopyable.hpp>

#include "folly/Portability.h"
#include "folly/Range.h"

namespace folly {

/**
 * C++ interface around Linux Async IO.
 */
class AsyncIO : private boost::noncopyable {
 public:
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
   */
  explicit AsyncIO(size_t capacity, PollMode pollMode=NOT_POLLABLE);
  ~AsyncIO();

  /**
   * An Op represents a pending operation.  You may inherit from Op (and
   * override onCompleted) in order to be notified of completion (see
   * CallbackOp below for an example), or you may use Op's methods directly.
   *
   * The Op must remain allocated until completion.
   */
  class Op : private boost::noncopyable {
    friend class AsyncIO;
   public:
    Op();
    virtual ~Op();

    // There would be a cancel() method here if Linux AIO actually implemented
    // it.  But let's not get your hopes up.

    enum State {
      UNINITIALIZED,
      PENDING,
      COMPLETED
    };

    /**
     * Return the current operation state.
     */
    State state() const { return state_; }

    /**
     * Reset the operation for reuse.  It is an error to call reset() on
     * an Op that is still pending.
     */
    void reset();

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
    void start();
    void complete(ssize_t result);

    virtual void onCompleted();

    State state_;
    ssize_t result_;
  };

  /**
   * Initiate a read request.
   */
  void pread(Op* op, int fd, void* buf, size_t size, off_t start);
  void pread(Op* op, int fd, Range<unsigned char*> range, off_t start);
  void preadv(Op* op, int fd, const iovec* iov, int iovcnt, off_t start);

  /**
   * Initiate a write request.
   */
  void pwrite(Op* op, int fd, const void* buf, size_t size, off_t start);
  void pwrite(Op* op, int fd, Range<const unsigned char*> range, off_t start);
  void pwritev(Op* op, int fd, const iovec* iov, int iovcnt, off_t start);

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

 private:
  void initializeContext();
  void submit(Op* op, iocb* cb);
  Range<Op**> doWait(size_t minRequests, size_t maxRequests);

  io_context_t ctx_;
  size_t pending_;
  size_t capacity_;
  int pollFd_;
  std::vector<Op*> completed_;
};

/**
 * Implementation of AsyncIO::Op that calls a callback and then deletes
 * itself.
 */
class CallbackOp : public AsyncIO::Op {
 public:
  typedef std::function<void(ssize_t)> Callback;
  static CallbackOp* make(Callback&& callback);

 private:
  explicit CallbackOp(Callback&& callback);
  ~CallbackOp();
  void onCompleted() FOLLY_OVERRIDE;

  Callback callback_;
};

}  // namespace folly

#endif /* FOLLY_IO_ASYNCIO_H_ */

