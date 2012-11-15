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

#ifndef FOLLY_CURSOR_H
#define FOLLY_CURSOR_H

#include <assert.h>
#include <stdexcept>
#include <string.h>
#include <type_traits>
#include <memory>

#include "folly/Bits.h"
#include "folly/experimental/io/IOBuf.h"
#include "folly/Likely.h"

/**
 * Cursor class for fast iteration over IOBuf chains.
 *
 * Cursor - Read-only access
 *
 * RWPrivateCursor - Read-write access, assumes private access to IOBuf chain
 * RWUnshareCursor - Read-write access, calls unshare on write (COW)
 * Appender        - Write access, assumes private access to IOBuf chian
 *
 * Note that RW cursors write in the preallocated part of buffers (that is,
 * between the buffer's data() and tail()), while Appenders append to the end
 * of the buffer (between the buffer's tail() and bufferEnd()).  Appenders
 * automatically adjust the buffer pointers, so you may only use one
 * Appender with a buffer chain; for this reason, Appenders assume private
 * access to the buffer (you need to call unshare() yourself if necessary).
 **/
namespace folly { namespace io {
namespace detail {

template <class Derived, typename BufType>
class CursorBase {
 public:
  const uint8_t* data() const {
    return crtBuf_->data() + offset_;
  }

  // Space available in the current IOBuf.  May be 0; use peek() instead which
  // will always point to a non-empty chunk of data or at the end of the
  // chain.
  size_t length() const {
    return crtBuf_->length() - offset_;
  }

  Derived& operator+=(size_t offset) {
    Derived* p = static_cast<Derived*>(this);
    p->skip(offset);
    return *p;
  }

  template <class T>
  typename std::enable_if<std::is_integral<T>::value, T>::type
  read() {
    T val;
    pull(&val, sizeof(T));
    return val;
  }

  template <class T>
  T readBE() {
    return Endian::big(read<T>());
  }

  template <class T>
  T readLE() {
    return Endian::little(read<T>());
  }

  explicit CursorBase(BufType* buf)
    : crtBuf_(buf)
    , offset_(0)
    , buffer_(buf) {}

  // Make all the templated classes friends for copy constructor.
  template <class D, typename B> friend class CursorBase;

  template <class T>
  explicit CursorBase(const T& cursor) {
    crtBuf_ = cursor.crtBuf_;
    offset_ = cursor.offset_;
    buffer_ = cursor.buffer_;
  }

  // reset cursor to point to a new buffer.
  void reset(BufType* buf) {
    crtBuf_ = buf;
    buffer_ = buf;
    offset_ = 0;
  }

  /**
   * Return the available data in the current buffer.
   * If you want to gather more data from the chain into a contiguous region
   * (for hopefully zero-copy access), use gather() before peek().
   */
  std::pair<const uint8_t*, size_t> peek() {
    // Ensure that we're pointing to valid data
    size_t available = length();
    while (UNLIKELY(available == 0 && tryAdvanceBuffer())) {
      available = length();
    }

    return std::make_pair(data(), available);
  }

  void pull(void* buf, size_t length) {
    if (UNLIKELY(pullAtMost(buf, length) != length)) {
      throw std::out_of_range("underflow");
    }
  }

  void clone(std::unique_ptr<folly::IOBuf>& buf, size_t length) {
    if (UNLIKELY(cloneAtMost(buf, length) != length)) {
      throw std::out_of_range("underflow");
    }
  }

  void skip(size_t length) {
    if (UNLIKELY(skipAtMost(length) != length)) {
      throw std::out_of_range("underflow");
    }
  }

  size_t pullAtMost(void* buf, size_t len) {
    uint8_t* p = reinterpret_cast<uint8_t*>(buf);
    size_t copied = 0;
    for (;;) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (LIKELY(available >= len)) {
        memcpy(p, data(), len);
        offset_ += len;
        return copied + len;
      }

      memcpy(p, data(), available);
      copied += available;
      if (UNLIKELY(!tryAdvanceBuffer())) {
        return copied;
      }
      p += available;
      len -= available;
    }
  }

  size_t cloneAtMost(std::unique_ptr<folly::IOBuf>& buf, size_t len) {
    buf.reset(nullptr);

    std::unique_ptr<folly::IOBuf> tmp;
    size_t copied = 0;
    for (;;) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (LIKELY(available >= len)) {
        tmp = crtBuf_->cloneOne();
        tmp->trimStart(offset_);
        tmp->trimEnd(tmp->length() - len);
        offset_ += len;
        if (!buf) {
          buf = std::move(tmp);
        } else {
          buf->prependChain(std::move(tmp));
        }
        return copied + len;
      }

      tmp = crtBuf_->cloneOne();
      tmp->trimStart(offset_);
      if (!buf) {
        buf = std::move(tmp);
      } else {
        buf->prependChain(std::move(tmp));
      }

      copied += available;
      if (UNLIKELY(!tryAdvanceBuffer())) {
        return copied;
      }
      len -= available;
    }
  }

  size_t skipAtMost(size_t len) {
    size_t skipped = 0;
    for (;;) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (LIKELY(available >= len)) {
        offset_ += len;
        return skipped + len;
      }

      skipped += available;
      if (UNLIKELY(!tryAdvanceBuffer())) {
        return skipped;
      }
      len -= available;
    }
  }

 protected:
  BufType* crtBuf_;
  size_t offset_;

  ~CursorBase(){}

  bool tryAdvanceBuffer() {
    BufType* nextBuf = crtBuf_->next();
    if (UNLIKELY(nextBuf == buffer_)) {
      offset_ = crtBuf_->length();
      return false;
    }

    offset_ = 0;
    crtBuf_ = nextBuf;
    static_cast<Derived*>(this)->advanceDone();
    return true;
  }

 private:
  void advanceDone() {
  }

  BufType* buffer_;
};

template <class Derived>
class Writable {
 public:
  template <class T>
  typename std::enable_if<std::is_integral<T>::value>::type
  write(T value) {
    const uint8_t* u8 = reinterpret_cast<const uint8_t*>(&value);
    push(u8, sizeof(T));
  }

  template <class T>
  void writeBE(T value) {
    write(Endian::big(value));
  }

  template <class T>
  void writeLE(T value) {
    write(Endian::little(value));
  }

  void push(const uint8_t* buf, size_t len) {
    Derived* d = static_cast<Derived*>(this);
    if (d->pushAtMost(buf, len) != len) {
      throw std::out_of_range("overflow");
    }
  }
};

} // namespace detail

class Cursor : public detail::CursorBase<Cursor, const IOBuf> {
 public:
  explicit Cursor(const IOBuf* buf)
    : detail::CursorBase<Cursor, const IOBuf>(buf) {}

  template <class CursorType>
  explicit Cursor(CursorType& cursor)
    : detail::CursorBase<Cursor, const IOBuf>(cursor) {}
};

enum class CursorAccess {
  PRIVATE,
  UNSHARE
};

template <CursorAccess access>
class RWCursor
  : public detail::CursorBase<RWCursor<access>, IOBuf>,
    public detail::Writable<RWCursor<access>> {
  friend class detail::CursorBase<RWCursor<access>, IOBuf>;
 public:
  explicit RWCursor(IOBuf* buf)
    : detail::CursorBase<RWCursor<access>, IOBuf>(buf),
      maybeShared_(true) {}

  template <class CursorType>
  explicit RWCursor(CursorType& cursor)
    : detail::CursorBase<RWCursor<access>, IOBuf>(cursor),
      maybeShared_(true) {}
  /**
   * Gather at least n bytes contiguously into the current buffer,
   * by coalescing subsequent buffers from the chain as necessary.
   */
  void gather(size_t n) {
    this->crtBuf_->gather(this->offset_ + n);
  }

  size_t pushAtMost(const uint8_t* buf, size_t len) {
    size_t copied = 0;
    for (;;) {
      // Fast path: the current buffer is big enough.
      size_t available = this->length();
      if (LIKELY(available >= len)) {
        if (access == CursorAccess::UNSHARE) {
          maybeUnshare();
        }
        memcpy(writableData(), buf, len);
        this->offset_ += len;
        return copied + len;
      }

      if (access == CursorAccess::UNSHARE) {
        maybeUnshare();
      }
      memcpy(writableData(), buf, available);
      copied += available;
      if (UNLIKELY(!this->tryAdvanceBuffer())) {
        return copied;
      }
      buf += available;
      len -= available;
    }
  }

  void insert(std::unique_ptr<folly::IOBuf> buf) {
    folly::IOBuf* nextBuf;
    if (this->offset_ == 0) {
      // Can just prepend
      nextBuf = buf.get();
      this->crtBuf_->prependChain(std::move(buf));
    } else {
      std::unique_ptr<folly::IOBuf> remaining;
      if (this->crtBuf_->length() - this->offset_ > 0) {
        // Need to split current IOBuf in two.
        remaining = this->crtBuf_->cloneOne();
        remaining->trimStart(this->offset_);
        nextBuf = remaining.get();
        buf->prependChain(std::move(remaining));
      } else {
        // Can just append
        nextBuf = this->crtBuf_->next();
      }
      this->crtBuf_->trimEnd(this->length());
      this->crtBuf_->appendChain(std::move(buf));
    }
    // Jump past the new links
    this->offset_ = 0;
    this->crtBuf_ = nextBuf;
  }

  uint8_t* writableData() {
    return this->crtBuf_->writableData() + this->offset_;
  }

 private:
  void maybeUnshare() {
    if (UNLIKELY(maybeShared_)) {
      this->crtBuf_->unshareOne();
      maybeShared_ = false;
    }
  }

  void advanceDone() {
    maybeShared_ = true;
  }

  bool maybeShared_;
};

typedef RWCursor<CursorAccess::PRIVATE> RWPrivateCursor;
typedef RWCursor<CursorAccess::UNSHARE> RWUnshareCursor;

/**
 * Append to the end of a buffer chain, growing the chain (by allocating new
 * buffers) in increments of at least growth bytes every time.  Won't grow
 * (and push() and ensure() will throw) if growth == 0.
 *
 * TODO(tudorb): add a flavor of Appender that reallocates one IOBuf instead
 * of chaining.
 */
class Appender : public detail::Writable<Appender> {
 public:
  Appender(IOBuf* buf, uint32_t growth)
    : buffer_(buf),
      crtBuf_(buf->prev()),
      growth_(growth) {
  }

  uint8_t* writableData() {
    return crtBuf_->writableTail();
  }

  size_t length() const {
    return crtBuf_->tailroom();
  }

  /**
   * Mark n bytes (must be <= length()) as appended, as per the
   * IOBuf::append() method.
   */
  void append(size_t n) {
    crtBuf_->append(n);
  }

  /**
   * Ensure at least n contiguous bytes available to write.
   * Postcondition: length() >= n.
   */
  void ensure(uint32_t n) {
    if (LIKELY(length() >= n)) {
      return;
    }

    // Waste the rest of the current buffer and allocate a new one.
    // Don't make it too small, either.
    if (growth_ == 0) {
      throw std::out_of_range("can't grow buffer chain");
    }

    n = std::max(n, growth_);
    buffer_->prependChain(IOBuf::create(n));
    crtBuf_ = buffer_->prev();
  }

  size_t pushAtMost(const uint8_t* buf, size_t len) {
    size_t copied = 0;
    for (;;) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (LIKELY(available >= len)) {
        memcpy(writableData(), buf, len);
        append(len);
        return copied + len;
      }

      memcpy(writableData(), buf, available);
      append(available);
      copied += available;
      if (UNLIKELY(!tryGrowChain())) {
        return copied;
      }
      buf += available;
      len -= available;
    }
  }

 private:
  bool tryGrowChain() {
    assert(crtBuf_->next() == buffer_);
    if (growth_ == 0) {
      return false;
    }

    buffer_->prependChain(IOBuf::create(growth_));
    crtBuf_ = buffer_->prev();
    return true;
  }

  IOBuf* buffer_;
  IOBuf* crtBuf_;
  uint32_t growth_;
};

}}  // folly::io

#endif // FOLLY_CURSOR_H
