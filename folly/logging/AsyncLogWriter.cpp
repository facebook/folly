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

#include <folly/logging/AsyncLogWriter.h>

#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/detail/AtFork.h>
#include <folly/logging/LoggerDB.h>
#include <folly/system/ThreadName.h>

namespace folly {

constexpr size_t AsyncLogWriter::kDefaultMaxBufferSize;

AsyncLogWriter::AsyncLogWriter() {
  folly::detail::AtFork::registerHandler(
      this,
      [this] { return preFork(); },
      [this] { postForkParent(); },
      [this] { postForkChild(); });

  // Start the I/O thread after registering the atfork handler.
  // preFork() may be invoked in another thread as soon as registerHandler()
  // returns.  It will check FLAG_IO_THREAD_STARTED to see if the I/O thread is
  // running yet.
  {
    auto data = data_.lock();
    data->flags |= FLAG_IO_THREAD_STARTED;
    data->ioThread = std::thread([this] { ioThread(); });
  }
}

AsyncLogWriter::~AsyncLogWriter() {
  {
    auto data = data_.lock();
    if (!(data->flags & FLAG_DESTROYING)) {
      LoggerDB::internalWarning(
          __FILE__, __LINE__, "cleanup() is not called before destroying");
      stopIoThread(data, FLAG_DESTROYING);
      assert(false);
    }
  }

  // Unregister the atfork handler after stopping the I/O thread.
  // preFork(), postForkParent(), and postForkChild() calls can run
  // concurrently with the destructor until unregisterHandler() returns.
  folly::detail::AtFork::unregisterHandler(this);
}

void AsyncLogWriter::cleanup() {
  std::vector<std::string>* ioQueue;
  size_t numDiscarded;
  {
    // Stop the I/O thread
    auto data = data_.lock();
    stopIoThread(data, FLAG_DESTROYING);

    // stopIoThread() causes the I/O thread to stop as soon as possible,
    // without waiting for all pending messages to be written.  Extract any
    // remaining messages to write them below.
    ioQueue = data->getCurrentQueue();
    numDiscarded = data->numDiscarded;
  }

  // If there are still any pending messages, flush them now.
  if (!ioQueue->empty()) {
    performIO(*ioQueue, numDiscarded);
  }
}

void AsyncLogWriter::writeMessage(StringPiece buffer, uint32_t flags) {
  return writeMessage(buffer.str(), flags);
}

void AsyncLogWriter::writeMessage(std::string&& buffer, uint32_t flags) {
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

void AsyncLogWriter::flush() {
  auto data = data_.lock();
  auto start = data->ioThreadCounter;

  // Wait until ioThreadCounter increments by at least two.
  // Waiting for a single increment is not sufficient, as this happens after
  // the I/O thread has swapped the queues, which is before it has actually
  // done the I/O.
  while (data->ioThreadCounter < start + 2) {
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

void AsyncLogWriter::setMaxBufferSize(size_t size) {
  auto data = data_.lock();
  data->maxBufferBytes = size;
}

size_t AsyncLogWriter::getMaxBufferSize() const {
  auto data = data_.lock();
  return data->maxBufferBytes;
}

void AsyncLogWriter::ioThread() {
  folly::setThreadName("log_writer");

  while (true) {
    // With the lock held, grab a pointer to the current queue, then increment
    // the ioThreadCounter index so that other threads will write into the
    // other queue as we process this one.
    std::vector<std::string>* ioQueue;
    size_t numDiscarded;
    {
      auto data = data_.lock();
      ioQueue = data->getCurrentQueue();
      while (ioQueue->empty() && !(data->flags & FLAG_STOP)) {
        // Wait for a message or one of the above flags to be set.
        messageReady_.wait(data.getUniqueLock());
      }

      if (data->flags & FLAG_STOP) {
        // We have been asked to stop.  We exit immediately in this case
        // without writing out any pending messages.  If we are stopping due
        // to a fork() the I/O thread will be restarted after the fork (as
        // long as we are not also being destroyed).  If we are stopping due
        // to the destructor, any remaining messages will be written out
        // inside the destructor.
        data->flags |= FLAG_IO_THREAD_STOPPED;
        data.unlock();
        ioCV_.notify_all();
        return;
      }

      ++data->ioThreadCounter;
      numDiscarded = data->numDiscarded;
      data->numDiscarded = 0;
      data->currentBufferSize = 0;
    }
    ioCV_.notify_all();

    // Write the log messages now that we have released the lock
    performIO(*ioQueue, numDiscarded);

    // clear() empties the vector, but the allocated capacity remains so we can
    // just reuse it without having to re-allocate in most cases.
    ioQueue->clear();
  }
}

bool AsyncLogWriter::preFork() {
  // Stop the I/O thread.
  //
  // It would perhaps be better to not stop the I/O thread in the parent
  // process.  However, this leaves us in a slightly tricky situation in the
  // child process where data_->ioThread has been initialized and does not
  // really point to a valid thread.  While we could store it in a union and
  // replace it without ever calling its destructor, in practice this still has
  // some tricky corner cases to deal with.

  // Grab the data lock to ensure no other thread is holding it
  // while we fork.
  lockedData_ = data_.lock();

  // If the I/O thread has been started, stop it now
  if (lockedData_->flags & FLAG_IO_THREAD_STARTED) {
    stopIoThread(lockedData_, 0);
  }

  return true;
}

void AsyncLogWriter::postForkParent() {
  // Restart the I/O thread
  restartThread();
}

void AsyncLogWriter::postForkChild() {
  // Clear any messages in the queue.  We only want them to be written once,
  // and we let the parent process handle writing them.
  lockedData_->queues[0].clear();
  lockedData_->queues[1].clear();

  // Restart the I/O thread
  restartThread();
}

void AsyncLogWriter::stopIoThread(
    folly::Synchronized<Data, std::mutex>::LockedPtr& data,
    uint32_t extraFlags) {
  data->flags |= (FLAG_STOP | extraFlags);
  messageReady_.notify_one();
  ioCV_.wait(data.getUniqueLock(), [&] {
    return bool(data->flags & FLAG_IO_THREAD_STOPPED);
  });

  // Check FLAG_IO_THREAD_JOINED before calling join().
  // preFork() and the destructor may both run concurrently in separate
  // threads, and only one should try to join the thread.
  if ((data->flags & FLAG_IO_THREAD_JOINED) == 0) {
    data->ioThread.join();
    data->flags |= FLAG_IO_THREAD_JOINED;
  }
}

void AsyncLogWriter::restartThread() {
  // Move lockedData_ into a local member variable so it will be released
  // when we return.
  folly::Synchronized<Data, std::mutex>::LockedPtr data =
      std::move(lockedData_);

  if (!(data->flags & FLAG_IO_THREAD_STARTED)) {
    // Do not start the I/O thread if the constructor has not finished yet
    return;
  }
  if (data->flags & FLAG_DESTROYING) {
    // Do not restart the I/O thread if we were being destroyed.
    // If there are more pending messages that need to be flushed the
    // destructor's stopIoThread() call will handle flushing the messages in
    // this case.
    return;
  }

  data->flags &= ~(FLAG_STOP | FLAG_IO_THREAD_JOINED | FLAG_IO_THREAD_STOPPED);
  data->ioThread = std::thread([this] { ioThread(); });
}

} // namespace folly
