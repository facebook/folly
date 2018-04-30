/*
 * Copyright 2017-present Facebook, Inc.
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
#include <folly/experimental/logging/AsyncFileWriter.h>

#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/experimental/logging/LoggerDB.h>
#include <folly/system/ThreadName.h>

using folly::File;
using folly::StringPiece;

namespace folly {

constexpr size_t AsyncFileWriter::kDefaultMaxBufferSize;

AsyncFileWriter::AsyncFileWriter(StringPiece path)
    : AsyncFileWriter{File{path.str(), O_WRONLY | O_APPEND | O_CREAT}} {}

AsyncFileWriter::AsyncFileWriter(folly::File&& file)
    : file_{std::move(file)}, ioThread_([this] { ioThread(); }) {}

AsyncFileWriter::~AsyncFileWriter() {
  data_->stop = true;
  messageReady_.notify_one();
  ioThread_.join();
}

void AsyncFileWriter::writeMessage(StringPiece buffer, uint32_t flags) {
  return writeMessage(buffer.str(), flags);
}

void AsyncFileWriter::writeMessage(std::string&& buffer, uint32_t flags) {
  auto data = data_.lock();
  if ((data->currentBufferSize >= data->maxBufferBytes) &&
      !(flags & NEVER_DISCARD)) {
    ++data->numDiscarded;
    return;
  }

  data->currentBufferSize += buffer.size();
  auto* queue = data->getCurrentQueue();
  queue->emplace_back(std::move(buffer));
  messageReady_.notify_one();
}

void AsyncFileWriter::flush() {
  auto data = data_.lock();
  auto start = data->ioThreadCounter;

  // Wait until ioThreadCounter increments by at least two.
  // Waiting for a single increment is not sufficient, as this happens after
  // the I/O thread has swapped the queues, which is before it has actually
  // done the I/O.
  while (data->ioThreadCounter < start + 2) {
    if (data->ioThreadDone) {
      return;
    }

    // Enqueue an empty string and wake the I/O thread.
    // The empty string ensures that the I/O thread will break out of its wait
    // loop and increment the ioThreadCounter, even if there is no other work
    // to do.
    data->getCurrentQueue()->emplace_back();
    messageReady_.notify_one();

    // Wait for notification from the I/O thread that it has done work.
    ioCV_.wait(data.getUniqueLock());
  }
}

void AsyncFileWriter::setMaxBufferSize(size_t size) {
  auto data = data_.lock();
  data->maxBufferBytes = size;
}

size_t AsyncFileWriter::getMaxBufferSize() const {
  auto data = data_.lock();
  return data->maxBufferBytes;
}

void AsyncFileWriter::ioThread() {
  folly::setThreadName("log_writer");

  while (true) {
    // With the lock held, grab a pointer to the current queue, then increment
    // the ioThreadCounter index so that other threads will write into the
    // other queue as we process this one.
    std::vector<std::string>* ioQueue;
    size_t numDiscarded;
    bool stop;
    {
      auto data = data_.lock();
      ioQueue = data->getCurrentQueue();
      while (ioQueue->empty() && !data->stop) {
        messageReady_.wait(data.getUniqueLock());
      }

      ++data->ioThreadCounter;
      numDiscarded = data->numDiscarded;
      data->numDiscarded = 0;
      data->currentBufferSize = 0;
      stop = data->stop;
    }
    ioCV_.notify_all();

    // Write the log messages now that we have released the lock
    try {
      performIO(ioQueue);
    } catch (const std::exception& ex) {
      onIoError(ex);
    }

    // clear() empties the vector, but the allocated capacity remains so we can
    // just reuse it without having to re-allocate in most cases.
    ioQueue->clear();

    if (numDiscarded > 0) {
      auto msg = getNumDiscardedMsg(numDiscarded);
      if (!msg.empty()) {
        auto ret = folly::writeFull(file_.fd(), msg.data(), msg.size());
        // We currently ignore errors from writeFull() here.
        // There's not much we can really do.
        (void)ret;
      }
    }

    if (stop) {
      data_->ioThreadDone = true;
      break;
    }
  }
}

void AsyncFileWriter::performIO(std::vector<std::string>* ioQueue) {
  // kNumIovecs controls the maximum number of strings we write at once in a
  // single writev() call.
  constexpr int kNumIovecs = 64;
  std::array<iovec, kNumIovecs> iovecs;

  size_t idx = 0;
  while (idx < ioQueue->size()) {
    int numIovecs = 0;
    while (numIovecs < kNumIovecs && idx < ioQueue->size()) {
      const auto& str = (*ioQueue)[idx];
      iovecs[numIovecs].iov_base = const_cast<char*>(str.data());
      iovecs[numIovecs].iov_len = str.size();
      ++numIovecs;
      ++idx;
    }

    auto ret = folly::writevFull(file_.fd(), iovecs.data(), numIovecs);
    folly::checkUnixError(ret, "writeFull() failed");
  }
}

void AsyncFileWriter::onIoError(const std::exception& ex) {
  LoggerDB::internalWarning(
      __FILE__,
      __LINE__,
      "error writing to log file ",
      file_.fd(),
      " in AsyncFileWriter: ",
      folly::exceptionStr(ex));
}

std::string AsyncFileWriter::getNumDiscardedMsg(size_t numDiscarded) {
  // We may want to make this customizable in the future (e.g., to allow it to
  // conform to the LogFormatter style being used).
  // For now just return a simple fixed message.
  return folly::to<std::string>(
      numDiscarded,
      " log messages discarded: logging faster than we can write\n");
}
} // namespace folly
