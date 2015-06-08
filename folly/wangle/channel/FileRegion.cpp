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
#include <folly/wangle/channel/FileRegion.h>

using namespace folly;
using namespace folly::wangle;

namespace {

struct FileRegionReadPool {};

Singleton<IOThreadPoolExecutor, FileRegionReadPool> readPool(
  []{
    return new IOThreadPoolExecutor(
        sysconf(_SC_NPROCESSORS_ONLN),
        std::make_shared<NamedThreadFactory>("FileRegionReadPool"));
  });

}

namespace folly { namespace wangle {

FileRegion::FileWriteRequest::FileWriteRequest(AsyncSocket* socket,
    WriteCallback* callback, int fd, off_t offset, size_t count)
  : WriteRequest(socket, callback),
    readFd_(fd), offset_(offset), count_(count) {
}

void FileRegion::FileWriteRequest::destroy() {
  readBase_->runInEventBaseThread([this]{
    delete this;
  });
}

bool FileRegion::FileWriteRequest::performWrite() {
  if (!started_) {
    start();
    return true;
  }

  int flags = SPLICE_F_NONBLOCK | SPLICE_F_MORE;
  ssize_t spliced = ::splice(pipe_out_, nullptr,
                             socket_->getFd(), nullptr,
                             bytesInPipe_, flags);
  if (spliced == -1) {
    if (errno == EAGAIN) {
      return true;
    }
    return false;
  }

  bytesInPipe_ -= spliced;
  bytesWritten(spliced);
  return true;
}

void FileRegion::FileWriteRequest::consume() {
  // do nothing
}

bool FileRegion::FileWriteRequest::isComplete() {
  return totalBytesWritten_ == count_;
}

void FileRegion::FileWriteRequest::messageAvailable(size_t&& count) {
  bool shouldWrite = bytesInPipe_ == 0;
  bytesInPipe_ += count;
  if (shouldWrite) {
    socket_->writeRequestReady();
  }
}

#ifdef __GLIBC__
# if (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 9))
#   define GLIBC_AT_LEAST_2_9 1
#  endif
#endif

void FileRegion::FileWriteRequest::start() {
  started_ = true;
  readBase_ = readPool.get()->getEventBase();
  readBase_->runInEventBaseThread([this]{
    auto flags = fcntl(readFd_, F_GETFL);
    if (flags == -1) {
      fail(__func__, AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "fcntl F_GETFL failed", errno));
      return;
    }

    flags &= O_ACCMODE;
    if (flags == O_WRONLY) {
      fail(__func__, AsyncSocketException(
          AsyncSocketException::BAD_ARGS, "file not open for reading"));
      return;
    }

#ifndef GLIBC_AT_LEAST_2_9
    fail(__func__, AsyncSocketException(
        AsyncSocketException::NOT_SUPPORTED,
        "writeFile unsupported on glibc < 2.9"));
    return;
#else
    int pipeFds[2];
    if (::pipe2(pipeFds, O_NONBLOCK) == -1) {
      fail(__func__, AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "pipe2 failed", errno));
      return;
    }

    // Max size for unprevileged processes as set in /proc/sys/fs/pipe-max-size
    // Ignore failures and just roll with it
    // TODO maybe read max size from /proc?
    fcntl(pipeFds[0], F_SETPIPE_SZ, 1048576);
    fcntl(pipeFds[1], F_SETPIPE_SZ, 1048576);

    pipe_out_ = pipeFds[0];

    socket_->getEventBase()->runInEventBaseThreadAndWait([&]{
      startConsuming(socket_->getEventBase(), &queue_);
    });
    readHandler_ = folly::make_unique<FileReadHandler>(
        this, pipeFds[1], count_);
#endif
  });
}

FileRegion::FileWriteRequest::~FileWriteRequest() {
  CHECK(readBase_->isInEventBaseThread());
  socket_->getEventBase()->runInEventBaseThreadAndWait([&]{
    stopConsuming();
    if (pipe_out_ > -1) {
      ::close(pipe_out_);
    }
  });

}

void FileRegion::FileWriteRequest::fail(
    const char* fn,
    const AsyncSocketException& ex) {
  socket_->getEventBase()->runInEventBaseThread([=]{
    WriteRequest::fail(fn, ex);
  });
}

FileRegion::FileWriteRequest::FileReadHandler::FileReadHandler(
    FileWriteRequest* req, int pipe_in, size_t bytesToRead)
  : req_(req), pipe_in_(pipe_in), bytesToRead_(bytesToRead) {
  CHECK(req_->readBase_->isInEventBaseThread());
  initHandler(req_->readBase_, pipe_in);
  if (!registerHandler(EventFlags::WRITE | EventFlags::PERSIST)) {
    req_->fail(__func__, AsyncSocketException(
        AsyncSocketException::INTERNAL_ERROR,
        "registerHandler failed"));
  }
}

FileRegion::FileWriteRequest::FileReadHandler::~FileReadHandler() {
  CHECK(req_->readBase_->isInEventBaseThread());
  unregisterHandler();
  ::close(pipe_in_);
}

void FileRegion::FileWriteRequest::FileReadHandler::handlerReady(
    uint16_t events) noexcept {
  CHECK(events & EventHandler::WRITE);
  if (bytesToRead_ == 0) {
    unregisterHandler();
    return;
  }

  int flags = SPLICE_F_NONBLOCK | SPLICE_F_MORE;
  ssize_t spliced = ::splice(req_->readFd_, &req_->offset_,
                             pipe_in_, nullptr,
                             bytesToRead_, flags);
  if (spliced == -1) {
    if (errno == EAGAIN) {
      return;
    } else {
      req_->fail(__func__, AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "splice failed", errno));
      return;
    }
  }

  if (spliced > 0) {
    bytesToRead_ -= spliced;
    try {
      req_->queue_.putMessage(static_cast<size_t>(spliced));
    } catch (...) {
      req_->fail(__func__, AsyncSocketException(
          AsyncSocketException::INTERNAL_ERROR,
          "putMessage failed"));
      return;
    }
  }
}
}} // folly::wangle
