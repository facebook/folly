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

#ifndef FOLLY_IO_STREAM_H_
#error This file may only be included from Stream.h
#endif

#include <string.h>

#include <glog/logging.h>

namespace folly {

template <class Stream>
InputByteStreamSplitter<Stream>::InputByteStreamSplitter(
    char delimiter, Stream stream)
  : done_(false),
    delimiter_(delimiter),
    stream_(std::move(stream)) {
}

template <class Stream>
bool InputByteStreamSplitter<Stream>::operator()(ByteRange& chunk) {
  DCHECK(!buffer_ || buffer_->length() == 0);
  chunk.clear();
  if (rest_.empty()) {
    if (done_) {
      return false;
    } else if (!stream_(rest_)) {
      done_ = true;
      return false;
    }
  }

  auto p = static_cast<const unsigned char*>(memchr(rest_.data(), delimiter_,
                                                    rest_.size()));
  if (p) {
    chunk.assign(rest_.data(), p);
    rest_.assign(p + 1, rest_.end());
    return true;
  }

  // Incomplete line read, copy to buffer
  if (!buffer_) {
    static const size_t kDefaultLineSize = 256;
    // Arbitrarily assume that we have half of a line in rest_, and
    // get enough room for twice that.
    buffer_ = IOBuf::create(std::max(kDefaultLineSize, 2 * rest_.size()));
  } else {
    buffer_->reserve(0, rest_.size());
  }
  memcpy(buffer_->writableTail(), rest_.data(), rest_.size());
  buffer_->append(rest_.size());

  while (stream_(rest_)) {
    auto p = static_cast<const unsigned char*>(
        memchr(rest_.data(), delimiter_, rest_.size()));
    if (p) {
      // Copy everything up to the delimiter and return it
      size_t n = p - rest_.data();
      buffer_->reserve(0, n);
      memcpy(buffer_->writableTail(), rest_.data(), n);
      buffer_->append(n);
      chunk.reset(buffer_->data(), buffer_->length());
      buffer_->trimStart(buffer_->length());
      rest_.assign(p + 1, rest_.end());
      return true;
    }

    // Nope, copy the entire chunk that we read
    buffer_->reserve(0, rest_.size());
    memcpy(buffer_->writableTail(), rest_.data(), rest_.size());
    buffer_->append(rest_.size());
  }

  // Incomplete last line
  done_ = true;
  rest_.clear();
  chunk.reset(buffer_->data(), buffer_->length());
  buffer_->trimStart(buffer_->length());
  return true;
}

}  // namespace folly

