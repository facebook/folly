/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/experimental/io/Stream.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdexcept>
#include <system_error>

#include "folly/String.h"

namespace folly {

FileInputByteStream::FileInputByteStream(int fd, bool ownsFd, size_t bufferSize)
  : fd_(fd),
    ownsFd_(ownsFd),
    buffer_(IOBuf::create(bufferSize)) {
}

FileInputByteStream::FileInputByteStream(int fd, bool ownsFd,
                                 std::unique_ptr<IOBuf>&& buffer)
  : fd_(fd),
    ownsFd_(ownsFd),
    buffer_(std::move(buffer)) {
  buffer_->clear();
}

bool FileInputByteStream::operator()(ByteRange& chunk) {
  ssize_t n = ::read(fd_, buffer_->writableTail(), buffer_->capacity());
  if (n == -1) {
    throw std::system_error(errno, std::system_category(), "read failed");
  }
  chunk.reset(buffer_->tail(), n);
  return (n != 0);
}

FileInputByteStream::FileInputByteStream(FileInputByteStream&& other)
  : fd_(other.fd_),
    ownsFd_(other.ownsFd_),
    buffer_(std::move(other.buffer_)) {
  other.fd_ = -1;
  other.ownsFd_ = false;
}

FileInputByteStream& FileInputByteStream::operator=(
    FileInputByteStream&& other) {
  if (&other != this) {
    closeNoThrow();
    fd_ = other.fd_;
    ownsFd_ = other.ownsFd_;
    buffer_ = std::move(other.buffer_);
    other.fd_ = -1;
    other.ownsFd_ = false;
  }
  return *this;
}

FileInputByteStream::~FileInputByteStream() {
  closeNoThrow();
}

void FileInputByteStream::closeNoThrow() {
  if (!ownsFd_) {
    return;
  }
  ownsFd_ = false;
  if (::close(fd_) == -1) {
    PLOG(ERROR) << "close failed";
  }
}

InputByteStreamSplitter<FileInputByteStream> byLine(
    const char* fileName, char delim) {
  int fd = ::open(fileName, O_RDONLY);
  if (fd == -1) {
    throw std::system_error(errno, std::system_category(), "open failed");
  }
  return makeInputByteStreamSplitter(delim, FileInputByteStream(fd, true));
}

}  // namespace folly

