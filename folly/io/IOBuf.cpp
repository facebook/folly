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

#define __STDC_LIMIT_MACROS

#include "folly/io/IOBuf.h"

#include "folly/Malloc.h"
#include "folly/Likely.h"

#include <stdexcept>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

using std::unique_ptr;

namespace folly {

const uint32_t IOBuf::kMaxIOBufSize;
// Note: Applying offsetof() to an IOBuf is legal according to C++11, since
// IOBuf is a standard-layout class.  However, this isn't legal with earlier
// C++ standards, which require that offsetof() only be used with POD types.
//
// This code compiles with g++ 4.6, but not with g++ 4.4 or earlier versions.
const uint32_t IOBuf::kMaxInternalDataSize =
  kMaxIOBufSize - offsetof(folly::IOBuf, int_.buf);

IOBuf::SharedInfo::SharedInfo()
  : freeFn(NULL),
    userData(NULL) {
  // Use relaxed memory ordering here.  Since we are creating a new SharedInfo,
  // no other threads should be referring to it yet.
  refcount.store(1, std::memory_order_relaxed);
}

IOBuf::SharedInfo::SharedInfo(FreeFunction fn, void* arg)
  : freeFn(fn),
    userData(arg) {
  // Use relaxed memory ordering here.  Since we are creating a new SharedInfo,
  // no other threads should be referring to it yet.
  refcount.store(1, std::memory_order_relaxed);
}

void* IOBuf::operator new(size_t size) {
  // Since IOBuf::create() manually allocates space for some IOBuf objects
  // using malloc(), override operator new so that all IOBuf objects are
  // always allocated using malloc().  This way operator delete can always know
  // that free() is the correct way to deallocate the memory.
  void* ptr = malloc(size);

  // operator new is not allowed to return NULL
  if (UNLIKELY(ptr == NULL)) {
    throw std::bad_alloc();
  }

  return ptr;
}

void* IOBuf::operator new(size_t size, void* ptr) {
  assert(size <= kMaxIOBufSize);
  return ptr;
}

void IOBuf::operator delete(void* ptr) {
  // For small buffers, IOBuf::create() manually allocates the space for the
  // IOBuf object using malloc().  Therefore we override delete to ensure that
  // the IOBuf space is freed using free() rather than a normal delete.
  free(ptr);
}

unique_ptr<IOBuf> IOBuf::create(uint32_t capacity) {
  // If the desired capacity is less than kMaxInternalDataSize,
  // just allocate a single region large enough for both the IOBuf header and
  // the data.
  if (capacity <= kMaxInternalDataSize) {
    void* buf = malloc(kMaxIOBufSize);
    if (UNLIKELY(buf == NULL)) {
      throw std::bad_alloc();
    }

    uint8_t* bufEnd = static_cast<uint8_t*>(buf) + kMaxIOBufSize;
    unique_ptr<IOBuf> iobuf(new(buf) IOBuf(bufEnd));
    assert(iobuf->capacity() >= capacity);
    return iobuf;
  }

  // Allocate an external buffer
  uint8_t* buf;
  SharedInfo* sharedInfo;
  uint32_t actualCapacity;
  allocExtBuffer(capacity, &buf, &sharedInfo, &actualCapacity);

  // Allocate the IOBuf header
  try {
    return unique_ptr<IOBuf>(new IOBuf(kExtAllocated, 0,
                                       buf, actualCapacity,
                                       buf, 0,
                                       sharedInfo));
  } catch (...) {
    free(buf);
    throw;
  }
}

unique_ptr<IOBuf> IOBuf::createChain(
    size_t totalCapacity, uint32_t maxBufCapacity) {
  unique_ptr<IOBuf> out = create(
      std::min(totalCapacity, size_t(maxBufCapacity)));
  size_t allocatedCapacity = out->capacity();

  while (allocatedCapacity < totalCapacity) {
    unique_ptr<IOBuf> newBuf = create(
        std::min(totalCapacity - allocatedCapacity, size_t(maxBufCapacity)));
    allocatedCapacity += newBuf->capacity();
    out->prependChain(std::move(newBuf));
  }

  return out;
}

unique_ptr<IOBuf> IOBuf::takeOwnership(void* buf, uint32_t capacity,
                                       uint32_t length,
                                       FreeFunction freeFn,
                                       void* userData,
                                       bool freeOnError) {
  SharedInfo* sharedInfo = NULL;
  try {
    sharedInfo = new SharedInfo(freeFn, userData);

    uint8_t* bufPtr = static_cast<uint8_t*>(buf);
    return unique_ptr<IOBuf>(new IOBuf(kExtUserSupplied, kFlagFreeSharedInfo,
                                       bufPtr, capacity,
                                       bufPtr, length,
                                       sharedInfo));
  } catch (...) {
    delete sharedInfo;
    if (freeOnError) {
      if (freeFn) {
        try {
          freeFn(buf, userData);
        } catch (...) {
          // The user's free function is not allowed to throw.
          abort();
        }
      } else {
        free(buf);
      }
    }
    throw;
  }
}

unique_ptr<IOBuf> IOBuf::wrapBuffer(const void* buf, uint32_t capacity) {
  // We cast away the const-ness of the buffer here.
  // This is okay since IOBuf users must use unshare() to create a copy of
  // this buffer before writing to the buffer.
  uint8_t* bufPtr = static_cast<uint8_t*>(const_cast<void*>(buf));
  return unique_ptr<IOBuf>(new IOBuf(kExtUserSupplied, kFlagUserOwned,
                                     bufPtr, capacity,
                                     bufPtr, capacity,
                                     NULL));
}

IOBuf::IOBuf(uint8_t* end)
  : next_(this),
    prev_(this),
    data_(int_.buf),
    length_(0),
    flags_(0) {
  assert(end - int_.buf == kMaxInternalDataSize);
  assert(end - reinterpret_cast<uint8_t*>(this) == kMaxIOBufSize);
}

IOBuf::IOBuf(ExtBufTypeEnum type,
             uint32_t flags,
             uint8_t* buf,
             uint32_t capacity,
             uint8_t* data,
             uint32_t length,
             SharedInfo* sharedInfo)
  : next_(this),
    prev_(this),
    data_(data),
    length_(length),
    flags_(kFlagExt | flags) {
  ext_.capacity = capacity;
  ext_.type = type;
  ext_.buf = buf;
  ext_.sharedInfo = sharedInfo;

  assert(data >= buf);
  assert(data + length <= buf + capacity);
  assert(static_cast<bool>(flags & kFlagUserOwned) ==
         (sharedInfo == NULL));
}

IOBuf::~IOBuf() {
  // Destroying an IOBuf destroys the entire chain.
  // Users of IOBuf should only explicitly delete the head of any chain.
  // The other elements in the chain will be automatically destroyed.
  while (next_ != this) {
    // Since unlink() returns unique_ptr() and we don't store it,
    // it will automatically delete the unlinked element.
    (void)next_->unlink();
  }

  if (flags_ & kFlagExt) {
    decrementRefcount();
  }
}

bool IOBuf::empty() const {
  const IOBuf* current = this;
  do {
    if (current->length() != 0) {
      return false;
    }
    current = current->next_;
  } while (current != this);
  return true;
}

uint32_t IOBuf::countChainElements() const {
  uint32_t numElements = 1;
  for (IOBuf* current = next_; current != this; current = current->next_) {
    ++numElements;
  }
  return numElements;
}

uint64_t IOBuf::computeChainDataLength() const {
  uint64_t fullLength = length_;
  for (IOBuf* current = next_; current != this; current = current->next_) {
    fullLength += current->length_;
  }
  return fullLength;
}

void IOBuf::prependChain(unique_ptr<IOBuf>&& iobuf) {
  // Take ownership of the specified IOBuf
  IOBuf* other = iobuf.release();

  // Remember the pointer to the tail of the other chain
  IOBuf* otherTail = other->prev_;

  // Hook up prev_->next_ to point at the start of the other chain,
  // and other->prev_ to point at prev_
  prev_->next_ = other;
  other->prev_ = prev_;

  // Hook up otherTail->next_ to point at us,
  // and prev_ to point back at otherTail,
  otherTail->next_ = this;
  prev_ = otherTail;
}

unique_ptr<IOBuf> IOBuf::clone() const {
  unique_ptr<IOBuf> newHead(cloneOne());

  for (IOBuf* current = next_; current != this; current = current->next_) {
    newHead->prependChain(current->cloneOne());
  }

  return newHead;
}

unique_ptr<IOBuf> IOBuf::cloneOne() const {
  if (flags_ & kFlagExt) {
    unique_ptr<IOBuf> iobuf(new IOBuf(static_cast<ExtBufTypeEnum>(ext_.type),
                                      flags_, ext_.buf, ext_.capacity,
                                      data_, length_,
                                      ext_.sharedInfo));
    if (ext_.sharedInfo) {
      ext_.sharedInfo->refcount.fetch_add(1, std::memory_order_acq_rel);
    }
    return iobuf;
  } else {
    // We have an internal data buffer that cannot be shared
    // Allocate a new IOBuf and copy the data into it.
    unique_ptr<IOBuf> iobuf(IOBuf::create(kMaxInternalDataSize));
    assert((iobuf->flags_ & kFlagExt) == 0);
    iobuf->data_ += headroom();
    memcpy(iobuf->data_, data_, length_);
    iobuf->length_ = length_;
    return iobuf;
  }
}

void IOBuf::unshareOneSlow() {
  // Internal buffers are always unshared, so unshareOneSlow() can only be
  // called for external buffers
  assert(flags_ & kFlagExt);

  // Allocate a new buffer for the data
  uint8_t* buf;
  SharedInfo* sharedInfo;
  uint32_t actualCapacity;
  allocExtBuffer(ext_.capacity, &buf, &sharedInfo, &actualCapacity);

  // Copy the data
  // Maintain the same amount of headroom.  Since we maintained the same
  // minimum capacity we also maintain at least the same amount of tailroom.
  uint32_t headlen = headroom();
  memcpy(buf + headlen, data_, length_);

  // Release our reference on the old buffer
  decrementRefcount();
  // Make sure kFlagExt is set, and kFlagUserOwned and kFlagFreeSharedInfo
  // are not set.
  flags_ = kFlagExt;

  // Update the buffer pointers to point to the new buffer
  data_ = buf + headlen;
  ext_.buf = buf;
  ext_.sharedInfo = sharedInfo;
}

void IOBuf::unshareChained() {
  // unshareChained() should only be called if we are part of a chain of
  // multiple IOBufs.  The caller should have already verified this.
  assert(isChained());

  IOBuf* current = this;
  while (true) {
    if (current->isSharedOne()) {
      // we have to unshare
      break;
    }

    current = current->next_;
    if (current == this) {
      // None of the IOBufs in the chain are shared,
      // so return without doing anything
      return;
    }
  }

  // We have to unshare.  Let coalesceSlow() do the work.
  coalesceSlow();
}

void IOBuf::coalesceSlow(size_t maxLength) {
  // coalesceSlow() should only be called if we are part of a chain of multiple
  // IOBufs.  The caller should have already verified this.
  assert(isChained());
  assert(length_ < maxLength);

  // Compute the length of the entire chain
  uint64_t newLength = 0;
  IOBuf* end = this;
  do {
    newLength += end->length_;
    end = end->next_;
  } while (newLength < maxLength && end != this);

  uint64_t newHeadroom = headroom();
  uint64_t newTailroom = end->prev_->tailroom();
  coalesceAndReallocate(newHeadroom, newLength, end, newTailroom);
  // We should be only element left in the chain now
  assert(length_ >= maxLength || !isChained());
}

void IOBuf::coalesceAndReallocate(size_t newHeadroom,
                                  size_t newLength,
                                  IOBuf* end,
                                  size_t newTailroom) {
  uint64_t newCapacity = newLength + newHeadroom + newTailroom;
  if (newCapacity > UINT32_MAX) {
    throw std::overflow_error("IOBuf chain too large to coalesce");
  }

  // Allocate space for the coalesced buffer.
  // We always convert to an external buffer, even if we happened to be an
  // internal buffer before.
  uint8_t* newBuf;
  SharedInfo* newInfo;
  uint32_t actualCapacity;
  allocExtBuffer(newCapacity, &newBuf, &newInfo, &actualCapacity);

  // Copy the data into the new buffer
  uint8_t* newData = newBuf + newHeadroom;
  uint8_t* p = newData;
  IOBuf* current = this;
  size_t remaining = newLength;
  do {
    assert(current->length_ <= remaining);
    remaining -= current->length_;
    memcpy(p, current->data_, current->length_);
    p += current->length_;
    current = current->next_;
  } while (current != end);
  assert(remaining == 0);

  // Point at the new buffer
  if (flags_ & kFlagExt) {
    decrementRefcount();
  }

  // Make sure kFlagExt is set, and kFlagUserOwned and kFlagFreeSharedInfo
  // are not set.
  flags_ = kFlagExt;

  ext_.capacity = actualCapacity;
  ext_.type = kExtAllocated;
  ext_.buf = newBuf;
  ext_.sharedInfo = newInfo;
  data_ = newData;
  length_ = newLength;

  // Separate from the rest of our chain.
  // Since we don't store the unique_ptr returned by separateChain(),
  // this will immediately delete the returned subchain.
  if (isChained()) {
    (void)separateChain(next_, current->prev_);
  }
}

void IOBuf::decrementRefcount() {
  assert(flags_ & kFlagExt);

  // Externally owned buffers don't have a SharedInfo object and aren't managed
  // by the reference count
  if (flags_ & kFlagUserOwned) {
    assert(ext_.sharedInfo == NULL);
    return;
  }

  // Decrement the refcount
  uint32_t newcnt = ext_.sharedInfo->refcount.fetch_sub(
      1, std::memory_order_acq_rel);
  // Note that fetch_sub() returns the value before we decremented.
  // If it is 1, we were the only remaining user; if it is greater there are
  // still other users.
  if (newcnt > 1) {
    return;
  }

  // We were the last user.  Free the buffer
  if (ext_.sharedInfo->freeFn != NULL) {
    try {
      ext_.sharedInfo->freeFn(ext_.buf, ext_.sharedInfo->userData);
    } catch (...) {
      // The user's free function should never throw.  Otherwise we might
      // throw from the IOBuf destructor.  Other code paths like coalesce()
      // also assume that decrementRefcount() cannot throw.
      abort();
    }
  } else {
    free(ext_.buf);
  }

  // Free the SharedInfo if it was allocated separately.
  //
  // This is only used by takeOwnership().
  //
  // To avoid this special case handling in decrementRefcount(), we could have
  // takeOwnership() set a custom freeFn() that calls the user's free function
  // then frees the SharedInfo object.  (This would require that
  // takeOwnership() store the user's free function with its allocated
  // SharedInfo object.)  However, handling this specially with a flag seems
  // like it shouldn't be problematic.
  if (flags_ & kFlagFreeSharedInfo) {
    delete ext_.sharedInfo;
  }
}

void IOBuf::reserveSlow(uint32_t minHeadroom, uint32_t minTailroom) {
  size_t newCapacity = (size_t)length_ + minHeadroom + minTailroom;
  CHECK_LT(newCapacity, UINT32_MAX);

  // We'll need to reallocate the buffer.
  // There are a few options.
  // - If we have enough total room, move the data around in the buffer
  //   and adjust the data_ pointer.
  // - If we're using an internal buffer, we'll switch to an external
  //   buffer with enough headroom and tailroom.
  // - If we have enough headroom (headroom() >= minHeadroom) but not too much
  //   (so we don't waste memory), we can try one of two things, depending on
  //   whether we use jemalloc or not:
  //   - If using jemalloc, we can try to expand in place, avoiding a memcpy()
  //   - If not using jemalloc and we don't have too much to copy,
  //     we'll use realloc() (note that realloc might have to copy
  //     headroom + data + tailroom, see smartRealloc in folly/Malloc.h)
  // - Otherwise, bite the bullet and reallocate.
  if (headroom() + tailroom() >= minHeadroom + minTailroom) {
    uint8_t* newData = writableBuffer() + minHeadroom;
    memmove(newData, data_, length_);
    data_ = newData;
    return;
  }

  size_t newAllocatedCapacity = goodExtBufferSize(newCapacity);
  uint8_t* newBuffer = nullptr;
  uint32_t newHeadroom = 0;
  uint32_t oldHeadroom = headroom();

  if ((flags_ & kFlagExt) && length_ != 0 && oldHeadroom >= minHeadroom) {
    if (usingJEMalloc()) {
      size_t headSlack = oldHeadroom - minHeadroom;
      // We assume that tailroom is more useful and more important than
      // tailroom (not least because realloc / rallocm allow us to grow the
      // buffer at the tail, but not at the head)  So, if we have more headroom
      // than we need, we consider that "wasted".  We arbitrarily define "too
      // much" headroom to be 25% of the capacity.
      if (headSlack * 4 <= newCapacity) {
        size_t allocatedCapacity = capacity() + sizeof(SharedInfo);
        void* p = ext_.buf;
        if (allocatedCapacity >= jemallocMinInPlaceExpandable) {
          int r = rallocm(&p, &newAllocatedCapacity, newAllocatedCapacity,
                          0, ALLOCM_NO_MOVE);
          if (r == ALLOCM_SUCCESS) {
            newBuffer = static_cast<uint8_t*>(p);
            newHeadroom = oldHeadroom;
          } else if (r == ALLOCM_ERR_OOM) {
            // shouldn't happen as we don't actually allocate new memory
            // (due to ALLOCM_NO_MOVE)
            throw std::bad_alloc();
          }
          // if ALLOCM_ERR_NOT_MOVED, do nothing, fall back to
          // malloc/memcpy/free
        }
      }
    } else {  // Not using jemalloc
      size_t copySlack = capacity() - length_;
      if (copySlack * 2 <= length_) {
        void* p = realloc(ext_.buf, newAllocatedCapacity);
        if (UNLIKELY(p == nullptr)) {
          throw std::bad_alloc();
        }
        newBuffer = static_cast<uint8_t*>(p);
        newHeadroom = oldHeadroom;
      }
    }
  }

  // None of the previous reallocation strategies worked (or we're using
  // an internal buffer).  malloc/copy/free.
  if (newBuffer == nullptr) {
    void* p = malloc(newAllocatedCapacity);
    if (UNLIKELY(p == nullptr)) {
      throw std::bad_alloc();
    }
    newBuffer = static_cast<uint8_t*>(p);
    memcpy(newBuffer + minHeadroom, data_, length_);
    if (flags_ & kFlagExt) {
      free(ext_.buf);
    }
    newHeadroom = minHeadroom;
  }

  SharedInfo* info;
  uint32_t cap;
  initExtBuffer(newBuffer, newAllocatedCapacity, &info, &cap);

  flags_ = kFlagExt;

  ext_.capacity = cap;
  ext_.type = kExtAllocated;
  ext_.buf = newBuffer;
  ext_.sharedInfo = info;
  data_ = newBuffer + newHeadroom;
  // length_ is unchanged
}

void IOBuf::allocExtBuffer(uint32_t minCapacity,
                           uint8_t** bufReturn,
                           SharedInfo** infoReturn,
                           uint32_t* capacityReturn) {
  size_t mallocSize = goodExtBufferSize(minCapacity);
  uint8_t* buf = static_cast<uint8_t*>(malloc(mallocSize));
  if (UNLIKELY(buf == NULL)) {
    throw std::bad_alloc();
  }
  initExtBuffer(buf, mallocSize, infoReturn, capacityReturn);
  *bufReturn = buf;
}

size_t IOBuf::goodExtBufferSize(uint32_t minCapacity) {
  // Determine how much space we should allocate.  We'll store the SharedInfo
  // for the external buffer just after the buffer itself.  (We store it just
  // after the buffer rather than just before so that the code can still just
  // use free(ext_.buf) to free the buffer.)
  size_t minSize = static_cast<size_t>(minCapacity) + sizeof(SharedInfo);
  // Add room for padding so that the SharedInfo will be aligned on an 8-byte
  // boundary.
  minSize = (minSize + 7) & ~7;

  // Use goodMallocSize() to bump up the capacity to a decent size to request
  // from malloc, so we can use all of the space that malloc will probably give
  // us anyway.
  return goodMallocSize(minSize);
}

void IOBuf::initExtBuffer(uint8_t* buf, size_t mallocSize,
                          SharedInfo** infoReturn,
                          uint32_t* capacityReturn) {
  // Find the SharedInfo storage at the end of the buffer
  // and construct the SharedInfo.
  uint8_t* infoStart = (buf + mallocSize) - sizeof(SharedInfo);
  SharedInfo* sharedInfo = new(infoStart) SharedInfo;

  size_t actualCapacity = infoStart - buf;
  // On the unlikely possibility that the actual capacity is larger than can
  // fit in a uint32_t after adding room for the refcount and calling
  // goodMallocSize(), truncate downwards if necessary.
  if (actualCapacity >= UINT32_MAX) {
    *capacityReturn = UINT32_MAX;
  } else {
    *capacityReturn = actualCapacity;
  }

  *infoReturn = sharedInfo;
}

fbstring IOBuf::moveToFbString() {
  // Externally allocated buffers (malloc) are just fine, everything else needs
  // to be turned into one.
  if (flags_ != kFlagExt ||  // not malloc()-ed
      headroom() != 0 ||     // malloc()-ed block doesn't start at beginning
      tailroom() == 0 ||     // no room for NUL terminator
      isShared() ||          // shared
      isChained()) {         // chained
    // We might as well get rid of all head and tailroom if we're going
    // to reallocate; we need 1 byte for NUL terminator.
    coalesceAndReallocate(0, computeChainDataLength(), this, 1);
  }

  // Ensure NUL terminated
  *writableTail() = 0;
  fbstring str(reinterpret_cast<char*>(writableData()),
               length(),  capacity(),
               AcquireMallocatedString());

  // Reset to internal buffer.
  flags_ = 0;
  clear();
  return str;
}

IOBuf::Iterator IOBuf::cbegin() const {
  return Iterator(this, this);
}

IOBuf::Iterator IOBuf::cend() const {
  return Iterator(nullptr, nullptr);
}

folly::fbvector<struct iovec> IOBuf::getIov() const {
  folly::fbvector<struct iovec> iov;
  iov.reserve(countChainElements());
  IOBuf const* p = this;
  do {
    // some code can get confused by empty iovs, so skip them
    if (p->length() > 0) {
      iov.push_back({(void*)p->data(), p->length()});
    }
    p = p->next();
  } while (p != this);
  return iov;
}

} // folly
