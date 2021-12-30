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

#pragma once

#include <queue>

#include <folly/Synchronized.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/io/AsyncBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/ScopedEventBaseThread.h>

namespace folly {

class SimpleAsyncIO : public EventHandler {
 public:
  // SimpleAsyncIO is a wrapper around AsyncIO intended to hide all the details.
  //
  // Usage: just create an instance of SimpleAsyncIO and then issue IO with
  // pread and pwrite, no other effort required. e.g.:
  //
  //    auto tmpfile = folly::File::temporary();
  //    folly::SimpleAsyncIO aio;
  //    aio.pwrite(
  //        tmpfile.fd(),
  //        "hello world",
  //        /*size=*/11,
  //        /*offset=*/0,
  //        [](int rc) { LOG(INFO) << "Write completed with result " << rc; });
  //
  // IO is dispatched in the context of the calling thread; it may block briefly
  // to obtain a lock on shared resources, but will *not* block for IO
  // completion. If the IO queue is full (see maxRequests in Config, below),
  // IO fails with -EBUSY.
  //
  // IO is completed on the executor specified in the config (global CPU
  // executor by default).
  //
  // IO is completed by calling the callback function provided to pread/pwrite.
  // The single parameter to the callback is either a negative errno or the
  // number of bytes transferred.
  //
  // There is a "hidden" EventBase which polls for IO completion and dispatches
  // completion events to the executor. You may specify an existing EventBase in
  // the config (and you are then responsible for making sure the EventBase
  // instance outlives the SimpleAsyncIO instance). If you do not specify one, a
  // ScopedEventBaseThread instance will be created.
  //
  // Following structure defines the configuration of a SimpleAsyncIO instance,
  // in case you need to override the (sensible) defaults.
  //
  // Typical usage is something like:
  //
  //    SimpleAsyncIO io(SimpleAsyncIO::Config()
  //        .setMaxRequests(100)
  //        .setMode(SimpleAsyncIO::Mode::IOURING));
  //
  enum Mode { AIO, IOURING };
  struct Config {
    Config()
        : maxRequests_(1000),
          completionExecutor_(
              getKeepAliveToken(getUnsafeMutableGlobalCPUExecutor().get())),
          mode_(AIO),
          evb_(nullptr) {}
    Config& setMaxRequests(size_t maxRequests) {
      maxRequests_ = maxRequests;
      return *this;
    }
    Config& setCompletionExecutor(Executor::KeepAlive<> completionExecutor) {
      completionExecutor_ = completionExecutor;
      return *this;
    }
    Config& setMode(Mode mode) {
      mode_ = mode;
      return *this;
    }
    Config& setEventBase(EventBase* evb) {
      evb_ = evb;
      return *this;
    }

   private:
    size_t maxRequests_;
    Executor::KeepAlive<> completionExecutor_;
    Mode mode_;
    EventBase* evb_;

    friend class SimpleAsyncIO;
  };

  explicit SimpleAsyncIO(Config cfg = Config());
  virtual ~SimpleAsyncIO() override;

  using SimpleAsyncIOCompletor = Function<void(int rc)>;

  /* Initiate an asynchronous read request.
   *
   * Parameters and return value are same as pread(2).
   *
   * Completion is indicated by an asynchronous call to the given completor
   * callback. The sole parameter to the callback is the result of the
   * operation.
   */
  void pread(
      int fd,
      void* buf,
      size_t size,
      off_t start,
      SimpleAsyncIOCompletor completor);

  /* Initiate an asynchronous write request.
   *
   * Parameters and return value are same as pwrite(2).
   *
   * Completion is indicated by an asynchronous call to the given completor
   * callback. The sole parameter to the callback is the result of the
   * operation.
   */
  void pwrite(
      int fd,
      const void* data,
      size_t size,
      off_t offset,
      SimpleAsyncIOCompletor completor);

#if FOLLY_HAS_COROUTINES
  /* Coroutine version of pread().
   *
   * Identical to pread() except that result is obtained by co_await instead of
   * callback.
   */
  folly::coro::Task<int> co_pread(int fd, void* buf, size_t size, off_t start);
  /* Coroutine version of pwrite().
   *
   * Identical to pwrite() except that result is obtained by co_await instead of
   * callback.
   */
  folly::coro::Task<int> co_pwrite(
      int fd, const void* buf, size_t size, off_t start);
#endif

 private:
  std::unique_ptr<AsyncBaseOp> getOp();
  void putOp(std::unique_ptr<AsyncBaseOp>&&);

  void submitOp(
      Function<void(AsyncBaseOp*)> preparer, SimpleAsyncIOCompletor completor);

  virtual void handlerReady(uint16_t events) noexcept override;

  template <typename AsyncIOType>
  void init();

  size_t maxRequests_;
  Executor::KeepAlive<> completionExecutor_;
  std::unique_ptr<AsyncBase> asyncIO_;
  Synchronized<std::queue<std::unique_ptr<AsyncBaseOp>>> opsFreeList_;
  std::unique_ptr<ScopedEventBaseThread> evb_;
  bool terminating_;
  Baton<> drainedBaton_;
};

} // namespace folly
