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
#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include <folly/File.h>
#include <folly/Range.h>
#include <folly/Synchronized.h>
#include <folly/experimental/logging/LogWriter.h>

namespace folly {

/**
 * A LogWriter implementation that asynchronously writes to a file descriptor.
 *
 * This class performs the log I/O in a separarate thread.
 *
 * The advantage of this class over ImmediateFileWriter is that logging I/O can
 * never slow down or block your normal program operation.  If log messages are
 * generated faster than they can be written, messages will be dropped (and an
 * indication of how many messages were dropped will be written to the log file
 * when we are able to catch up a bit.)
 *
 * However, one downside is that if your program crashes, not all log messages
 * may have been written, so you may lose messages generated immediately before
 * the crash.
 */
class AsyncFileWriter : public LogWriter {
 public:
  /**
   * The default maximum buffer size.
   *
   * The comments for setMaxBufferSize() explain how this parameter is used.
   */
  static constexpr size_t kDefaultMaxBufferSize = 1024 * 1024;

  /**
   * Construct an AsyncFileWriter that appends to the file at the specified
   * path.
   */
  explicit AsyncFileWriter(folly::StringPiece path);

  /**
   * Construct an AsyncFileWriter that writes to the specified File object.
   */
  explicit AsyncFileWriter(folly::File&& file);

  ~AsyncFileWriter();

  void writeMessage(folly::StringPiece buffer, uint32_t flags = 0) override;
  void writeMessage(std::string&& buffer, uint32_t flags = 0) override;

  /**
   * Block until the I/O thread has finished writing all messages that
   * were already enqueued when flush() was called.
   */
  void flush() override;

  /**
   * Set the maximum buffer size for this AsyncFileWriter, in bytes.
   *
   * This controls the upper bound on how much unwritten data will be buffered
   * in memory.  If messages are being logged faster than they can be written
   * to output file, new messages will be discarded if they would cause the
   * amount of buffered data to exceed this limit.
   */
  void setMaxBufferSize(size_t size);

  /**
   * Get the maximum buffer size for this AsyncFileWriter, in bytes.
   */
  size_t getMaxBufferSize() const;

  /**
   * Get the output file.
   */
  const folly::File& getFile() const {
    return file_;
  }

 private:
  /*
   * A simple implementation using two queues.
   * All writer threads enqueue into one queue while the I/O thread is
   * processing the other.
   *
   * We could potentially also provide an implementation using folly::MPMCQueue
   * in the future, which may improve contention under very high write loads.
   */
  struct Data {
    std::array<std::vector<std::string>, 2> queues;
    bool stop{false};
    bool ioThreadDone{false};
    uint64_t ioThreadCounter{0};
    size_t maxBufferBytes{kDefaultMaxBufferSize};
    size_t currentBufferSize{0};
    size_t numDiscarded{0};

    std::vector<std::string>* getCurrentQueue() {
      return &queues[ioThreadCounter & 0x1];
    }
  };

  void ioThread();
  void performIO(std::vector<std::string>* ioQueue);

  void onIoError(const std::exception& ex);
  std::string getNumDiscardedMsg(size_t numDiscarded);

  folly::File file_;
  folly::Synchronized<Data, std::mutex> data_;
  /**
   * messageReady_ is signaled by writer threads whenever they add a new
   * message to the current queue.
   */
  std::condition_variable messageReady_;
  /**
   * ioCV_ is signaled by the I/O thread each time it increments
   * the ioThreadCounter (once each time around its loop).
   */
  std::condition_variable ioCV_;

  /**
   * The I/O thread.
   *
   * This should come last, since all other member variables need to be
   * constructed before the I/O thread starts.
   */
  std::thread ioThread_;
};
} // namespace folly
