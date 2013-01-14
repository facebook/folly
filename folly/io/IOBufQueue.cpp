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

#include "folly/io/IOBufQueue.h"

#include <string.h>

#include <stdexcept>

using std::make_pair;
using std::pair;
using std::unique_ptr;

namespace {

using folly::IOBuf;

const size_t MIN_ALLOC_SIZE = 2000;
const size_t MAX_ALLOC_SIZE = 8000; // Must fit within a uint32_t

/**
 * Convenience function to append chain src to chain dst.
 */
void
appendToChain(unique_ptr<IOBuf>& dst, unique_ptr<IOBuf>&& src) {
  if (dst == NULL) {
    dst = std::move(src);
  } else {
    dst->prev()->appendChain(std::move(src));
  }
}

} // anonymous namespace

namespace folly {

IOBufQueue::IOBufQueue(const Options& options)
  : options_(options),
    chainLength_(0) {
}

IOBufQueue::IOBufQueue(IOBufQueue&& other)
  : options_(other.options_),
    chainLength_(other.chainLength_),
    head_(std::move(other.head_)) {
  other.chainLength_ = 0;
}

IOBufQueue& IOBufQueue::operator=(IOBufQueue&& other) {
  if (&other != this) {
    options_ = other.options_;
    chainLength_ = other.chainLength_;
    head_ = std::move(other.head_);
    other.chainLength_ = 0;
  }
  return *this;
}

std::pair<void*, uint32_t>
IOBufQueue::headroom() {
  if (head_) {
    return std::make_pair(head_->writableBuffer(), head_->headroom());
  } else {
    return std::make_pair(nullptr, 0);
  }
}

void
IOBufQueue::markPrepended(uint32_t n) {
  if (n == 0) {
    return;
  }
  assert(head_);
  head_->prepend(n);
  if (options_.cacheChainLength) {
    chainLength_ += n;
  }
}

void
IOBufQueue::prepend(const void* buf, uint32_t n) {
  auto p = headroom();
  if (n > p.second) {
    throw std::overflow_error("Not enough room to prepend");
  }
  memcpy(static_cast<char*>(p.first) + p.second - n, buf, n);
  markPrepended(n);
}

void
IOBufQueue::append(unique_ptr<IOBuf>&& buf) {
  if (!buf) {
    return;
  }
  if (options_.cacheChainLength) {
    chainLength_ += buf->computeChainDataLength();
  }
  appendToChain(head_, std::move(buf));
}

void
IOBufQueue::append(IOBufQueue& other) {
  if (!other.head_) {
    return;
  }
  if (options_.cacheChainLength) {
    if (other.options_.cacheChainLength) {
      chainLength_ += other.chainLength_;
    } else {
      chainLength_ += other.head_->computeChainDataLength();
    }
  }
  appendToChain(head_, std::move(other.head_));
  other.chainLength_ = 0;
}

void
IOBufQueue::append(const void* buf, size_t len) {
  auto src = static_cast<const uint8_t*>(buf);
  while (len != 0) {
    if ((head_ == NULL) || head_->prev()->isSharedOne() ||
        (head_->prev()->tailroom() == 0)) {
      appendToChain(head_, std::move(
          IOBuf::create(std::max(MIN_ALLOC_SIZE,
              std::min(len, MAX_ALLOC_SIZE)))));
    }
    IOBuf* last = head_->prev();
    uint32_t copyLen = std::min(len, (size_t)last->tailroom());
    memcpy(last->writableTail(), src, copyLen);
    src += copyLen;
    last->append(copyLen);
    if (options_.cacheChainLength) {
      chainLength_ += copyLen;
    }
    len -= copyLen;
  }
}

void
IOBufQueue::wrapBuffer(const void* buf, size_t len, uint32_t blockSize) {
  auto src = static_cast<const uint8_t*>(buf);
  while (len != 0) {
    size_t n = std::min(len, size_t(blockSize));
    append(IOBuf::wrapBuffer(src, n));
    src += n;
    len -= n;
  }
}

pair<void*,uint32_t>
IOBufQueue::preallocate(uint32_t min, uint32_t newAllocationSize,
                        uint32_t max) {
  if (head_ != NULL) {
    // If there's enough space left over at the end of the queue, use that.
    IOBuf* last = head_->prev();
    if (!last->isSharedOne()) {
      uint32_t avail = last->tailroom();
      if (avail >= min) {
        return make_pair(last->writableTail(), std::min(max, avail));
      }
    }
  }
  // Allocate a new buffer of the requested max size.
  unique_ptr<IOBuf> newBuf(IOBuf::create(std::max(min, newAllocationSize)));
  appendToChain(head_, std::move(newBuf));
  IOBuf* last = head_->prev();
  return make_pair(last->writableTail(),
                   std::min(max, last->tailroom()));
}

void
IOBufQueue::postallocate(uint32_t n) {
  head_->prev()->append(n);
  if (options_.cacheChainLength) {
    chainLength_ += n;
  }
}

unique_ptr<IOBuf>
IOBufQueue::split(size_t n) {
  unique_ptr<IOBuf> result;
  while (n != 0) {
    if (head_ == NULL) {
      throw std::underflow_error(
          "Attempt to remove more bytes than are present in IOBufQueue");
    } else if (head_->length() <= n) {
      n -= head_->length();
      if (options_.cacheChainLength) {
        chainLength_ -= head_->length();
      }
      unique_ptr<IOBuf> remainder = head_->pop();
      appendToChain(result, std::move(head_));
      head_ = std::move(remainder);
    } else {
      unique_ptr<IOBuf> clone = head_->cloneOne();
      clone->trimEnd(clone->length() - n);
      appendToChain(result, std::move(clone));
      head_->trimStart(n);
      if (options_.cacheChainLength) {
        chainLength_ -= n;
      }
      break;
    }
  }
  return std::move(result);
}

void IOBufQueue::trimStart(size_t amount) {
  while (amount > 0) {
    if (!head_) {
      throw std::underflow_error(
        "Attempt to trim more bytes than are present in IOBufQueue");
    }
    if (head_->length() > amount) {
      head_->trimStart(amount);
      if (options_.cacheChainLength) {
        chainLength_ -= amount;
      }
      break;
    }
    amount -= head_->length();
    if (options_.cacheChainLength) {
      chainLength_ -= head_->length();
    }
    head_ = head_->pop();
  }
}

void IOBufQueue::trimEnd(size_t amount) {
  while (amount > 0) {
    if (!head_) {
      throw std::underflow_error(
        "Attempt to trim more bytes than are present in IOBufQueue");
    }
    if (head_->prev()->length() > amount) {
      head_->prev()->trimEnd(amount);
      if (options_.cacheChainLength) {
        chainLength_ -= amount;
      }
      break;
    }
    amount -= head_->prev()->length();
    if (options_.cacheChainLength) {
      chainLength_ -= head_->prev()->length();
    }
    unique_ptr<IOBuf> b = head_->prev()->unlink();

    // Null queue if we unlinked the head.
    if (b.get() == head_.get()) {
      head_.reset();
    }
  }
}

} // folly
