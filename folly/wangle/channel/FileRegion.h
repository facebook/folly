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
#pragma once

#include <folly/Singleton.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/NotificationQueue.h>
#include <folly/futures/Future.h>
#include <folly/futures/Promise.h>
#include <folly/wangle/concurrent/IOThreadPoolExecutor.h>

namespace folly { namespace wangle {

class FileRegion {
 public:
  FileRegion(int fd, off_t offset, size_t count)
    : fd_(fd), offset_(offset), count_(count) {}

  Future<Unit> transferTo(std::shared_ptr<AsyncTransport> transport) {
    auto socket = std::dynamic_pointer_cast<AsyncSocket>(
        transport);
    CHECK(socket);
    auto cb = new WriteCallback();
    auto f = cb->promise_.getFuture();
    auto req = new FileWriteRequest(socket.get(), cb, fd_, offset_, count_);
    socket->writeRequest(req);
    return f;
  }

 private:
  class WriteCallback : private AsyncSocket::WriteCallback {
    void writeSuccess() noexcept override {
      promise_.setValue();
      delete this;
    }

    void writeErr(size_t bytesWritten,
                  const AsyncSocketException& ex)
      noexcept override {
      promise_.setException(ex);
      delete this;
    }

    friend class FileRegion;
    folly::Promise<Unit> promise_;
  };

  const int fd_;
  const off_t offset_;
  const size_t count_;

  class FileWriteRequest : public AsyncSocket::WriteRequest,
                           public NotificationQueue<size_t>::Consumer {
   public:
    FileWriteRequest(AsyncSocket* socket, WriteCallback* callback,
                     int fd, off_t offset, size_t count);

    void destroy() override;

    bool performWrite() override;

    void consume() override;

    bool isComplete() override;

    void messageAvailable(size_t&& count) override;

    void start() override;

    class FileReadHandler : public folly::EventHandler {
     public:
      FileReadHandler(FileWriteRequest* req, int pipe_in, size_t bytesToRead);

      ~FileReadHandler();

      void handlerReady(uint16_t events) noexcept override;

     private:
      FileWriteRequest* req_;
      int pipe_in_;
      size_t bytesToRead_;
    };

   private:
    ~FileWriteRequest();

    void fail(const char* fn, const AsyncSocketException& ex);

    const int readFd_;
    off_t offset_;
    const size_t count_;
    bool started_{false};
    int pipe_out_{-1};

    size_t bytesInPipe_{0};
    folly::EventBase* readBase_;
    folly::NotificationQueue<size_t> queue_;
    std::unique_ptr<FileReadHandler> readHandler_;
  };
};

}} // folly::wangle
