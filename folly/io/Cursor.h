/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#pragma once

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include <folly/Likely.h>
#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <folly/lang/Bits.h>
#include <folly/lang/Exception.h>

/**
 * IOBuf Cursors provide fast iteration over IOBuf chains.
 *
 * - Cursor          - Read-only access
 * - RWPrivateCursor - Read-write access, assumes private access to IOBuf chain
 * - RWUnshareCursor - Read-write access, calls unshare on write (COW)
 * - Appender        - Write access, assumes private access to IOBuf chain
 *
 * Note that RW cursors write in the preallocated part of buffers (that is,
 * between the buffer's data() and tail()), while Appenders append to the end
 * of the buffer (between the buffer's tail() and bufferEnd()).  Appenders
 * automatically adjust the buffer pointers, so you may only use one
 * Appender with a buffer chain; for this reason, Appenders assume private
 * access to the buffer (you need to call unshare() yourself if necessary).
 *
 * @file Cursor.h
 */

namespace folly {
namespace io {

namespace detail {

template <class Derived, class BufType>
class CursorBase {
  // Make all the templated classes friends for copy constructor.
  template <class D, typename B>
  friend class CursorBase;

 public:
  /**
   * Construct a cursor wrapping an IOBuf.
   */
  explicit CursorBase(BufType* buf) : crtBuf_(buf), buffer_(buf) {
    if (crtBuf_) {
      crtPos_ = crtBegin_ = crtBuf_->data();
      crtEnd_ = crtBuf_->tail();
    }
  }

  /**
   * Constuct a bounded cursor wrapping an IOBuf.
   *
   * @param len An upper bound on the number of bytes available to this cursor.
   */
  CursorBase(BufType* buf, size_t len) : crtBuf_(buf), buffer_(buf) {
    if (crtBuf_) {
      crtPos_ = crtBegin_ = crtBuf_->data();
      crtEnd_ = crtBuf_->tail();
      if (uintptr_t(crtPos_) + len < uintptr_t(crtEnd_)) {
        crtEnd_ = crtPos_ + len;
      }
      remainingLen_ = len - (crtEnd_ - crtPos_);
    }
  }

  /**
   * Copy constructor.
   *
   * This also allows constructing a CursorBase from other derived types.
   * For instance, this allows constructing a Cursor from an RWPrivateCursor.
   */
  template <class OtherDerived, class OtherBuf>
  explicit CursorBase(const CursorBase<OtherDerived, OtherBuf>& cursor)
      : crtBuf_(cursor.crtBuf_),
        buffer_(cursor.buffer_),
        crtBegin_(cursor.crtBegin_),
        crtEnd_(cursor.crtEnd_),
        crtPos_(cursor.crtPos_),
        absolutePos_(cursor.absolutePos_),
        remainingLen_(cursor.remainingLen_) {}

  template <class OtherDerived, class OtherBuf>
  explicit CursorBase(
      const CursorBase<OtherDerived, OtherBuf>& cursor, size_t len)
      : crtBuf_(cursor.crtBuf_),
        buffer_(cursor.buffer_),
        crtBegin_(cursor.crtBegin_),
        crtEnd_(cursor.crtEnd_),
        crtPos_(cursor.crtPos_),
        absolutePos_(cursor.absolutePos_) {
    if (cursor.isBounded() && len > cursor.remainingLen_ + cursor.length()) {
      throw_exception<std::out_of_range>("underflow");
    }
    if (uintptr_t(crtPos_) + len < uintptr_t(crtEnd_)) {
      crtEnd_ = crtPos_ + len;
    }
    remainingLen_ = len - (crtEnd_ - crtPos_);
  }

  /**
   * Reset cursor to point to a new buffer.
   *
   * @methodset Modifiers
   */
  void reset(BufType* buf) {
    crtBuf_ = buf;
    buffer_ = buf;
    absolutePos_ = 0;
    remainingLen_ = std::numeric_limits<size_t>::max();
    if (crtBuf_) {
      crtPos_ = crtBegin_ = crtBuf_->data();
      crtEnd_ = crtBuf_->tail();
    }
  }

  /**
   * Get the current Cursor position relative to the head of IOBuf chain.
   *
   * @methodset Capacity
   */
  size_t getCurrentPosition() const {
    dcheckIntegrity();
    return (crtPos_ - crtBegin_) + absolutePos_;
  }

  /**
   * Get the data at the current cursor position.
   *
   * @methodset Accessors
   */
  const uint8_t* data() const {
    dcheckIntegrity();
    return crtPos_;
  }

  /**
   * Return the remaining space available in the current IOBuf.
   *
   * @methodset Capacity
   *
   * May return 0 if the cursor is at the end of an IOBuf.  Use peekBytes()
   * instead if you want to avoid this.  peekBytes() will advance to the next
   * non-empty IOBuf (up to the end of the chain) if the cursor is currently
   * pointing at the end of a buffer.
   */
  size_t length() const {
    dcheckIntegrity();
    return crtEnd_ - crtPos_;
  }

  /**
   * Return the space available until the end of the entire IOBuf chain.
   *
   * @methodset Capacity
   *
   * For bounded Cursors, return the available space until the boundary.
   */
  size_t totalLength() const {
    size_t len = 0;
    const IOBuf* buf = crtBuf_->next();
    while (buf != buffer_ && len < remainingLen_) {
      len += buf->length();
      buf = buf->next();
    }
    return std::min(len, remainingLen_) + length();
  }

  /**
   * Return true if the cursor could advance the specified number of bytes
   * from its current position.
   *
   * @methodset Capacity
   *
   * This is useful for applications that want to do checked reads instead of
   * catching exceptions and is more efficient than using totalLength as it
   * walks the minimal set of buffers in the chain to determine the result.
   */
  bool canAdvance(size_t amount) const {
    if (isBounded() && amount > remainingLen_ + length()) {
      return false;
    }
    const IOBuf* nextBuf = crtBuf_;
    size_t available = length();
    do {
      if (available >= amount) {
        return true;
      }
      amount -= available;
      nextBuf = nextBuf->next();
      available = nextBuf->length();
    } while (nextBuf != buffer_);
    return false;
  }

  /**
   * Return true if the cursor is at the end of the entire IOBuf chain.
   *
   * @methodset Capacity
   */
  bool isAtEnd() const {
    dcheckIntegrity();
    // Check for the simple cases first.
    if (crtPos_ != crtEnd_) {
      return false;
    }
    if (crtBuf_ == buffer_->prev()) {
      return true;
    }
    if (isBounded() && remainingLen_ == 0) {
      return true;
    }
    // We are at the end of a buffer, but it isn't the last buffer.
    // We might still be at the end if the remaining buffers in the chain are
    // empty.
    const IOBuf* buf = crtBuf_->next();
    while (buf != buffer_) {
      if (buf->length() > 0) {
        return false;
      }
      buf = buf->next();
    }
    return true;
  }

  /**
   * Advances the cursor to the end of the entire IOBuf chain.
   *
   * @methodset Modifiers
   */
  void advanceToEnd() {
    // Simple case, we're already in the last IOBuf.
    if (crtBuf_ == buffer_->prev()) {
      crtPos_ = crtEnd_;
      return;
    }

    auto* nextBuf = crtBuf_->next();
    while (nextBuf != buffer_) {
      if (isBounded() && remainingLen_ == 0) {
        crtPos_ = crtEnd_;
        return;
      }
      absolutePos_ += crtEnd_ - crtBegin_;

      crtBuf_ = nextBuf;
      nextBuf = crtBuf_->next();
      crtBegin_ = crtBuf_->data();
      crtEnd_ = crtBuf_->tail();
      if (isBounded()) {
        if (uintptr_t(crtBegin_) + remainingLen_ < uintptr_t(crtEnd_)) {
          crtEnd_ = crtBegin_ + remainingLen_;
        }
        remainingLen_ -= crtEnd_ - crtBegin_;
      }
      crtPos_ = crtEnd_;
      derived().advanceDone();
    }
  }

  /// Advance the cursor
  ///
  /// @methodset Modifiers
  Derived& operator+=(size_t offset) {
    Derived* p = static_cast<Derived*>(this);
    p->skip(offset);
    return *p;
  }
  /// Get a new cursor, advanced by offset from this cursor.
  ///
  /// @methodset Modifiers
  Derived operator+(size_t offset) const {
    Derived other(*this);
    other.skip(offset);
    return other;
  }

  /// Retreat the cursor
  ///
  /// @methodset Modifiers
  Derived& operator-=(size_t offset) {
    Derived* p = static_cast<Derived*>(this);
    p->retreat(offset);
    return *p;
  }
  /// Get a new cursor, retreated by offset from this cursor.
  ///
  /// @methodset Modifiers
  Derived operator-(size_t offset) const {
    Derived other(*this);
    other.retreat(offset);
    return other;
  }

  /**
   * Compare cursors for equality/inequality.
   *
   * @methodset Comparison
   *
   * Two cursors are equal if they are pointing to the same location in the
   * same IOBuf chain.
   */
  bool operator==(const CursorBase& other) const {
    const IOBuf* crtBuf = crtBuf_;
    auto crtPos = crtPos_;
    // We can be pointing to the end of a buffer chunk, find first non-empty.
    while (crtPos == crtBuf->tail() && crtBuf != buffer_->prev()) {
      crtBuf = crtBuf->next();
      crtPos = crtBuf->data();
    }

    const IOBuf* crtBufOther = other.crtBuf_;
    auto crtPosOther = other.crtPos_;
    // We can be pointing to the end of a buffer chunk, find first non-empty.
    while (crtPosOther == crtBufOther->tail() &&
           crtBufOther != other.buffer_->prev()) {
      crtBufOther = crtBufOther->next();
      crtPosOther = crtBufOther->data();
    }
    return (crtPos == crtPosOther) && (crtBuf == crtBufOther);
  }

  /// @copydoc operator==
  bool operator!=(const CursorBase& other) const { return !operator==(other); }

  /**
   * Attempt to read from the cursor.
   *
   * @methodset Consumers
   *
   * @param[out] val  Store the read value in this location.
   * @return True iff successful; If there are not enough bytes left in the
   * cursor, return false.
   * @note val might be modified even if tryRead returns false.
   */
  template <class T>
  typename std::enable_if<std::is_arithmetic<T>::value, bool>::type tryRead(
      T& val) {
    if (FOLLY_LIKELY(uintptr_t(crtPos_) + sizeof(T) <= uintptr_t(crtEnd_))) {
      val = loadUnaligned<T>(data());
      crtPos_ += sizeof(T);
      return true;
    }
    return pullAtMostSlow(&val, sizeof(T)) == sizeof(T);
  }

  /**
   * Attempt to read a Big-Endian integral from the cursor.
   *
   * @methodset Consumers
   *
   * @see tryRead
   */
  template <class T>
  bool tryReadBE(T& val) {
    const bool result = tryRead(val);
    val = Endian::big(val);
    return result;
  }

  /**
   * Attempt to read a Little-Endian integral from the cursor.
   *
   * @methodset Consumers
   *
   * @see tryRead
   */
  template <class T>
  bool tryReadLE(T& val) {
    const bool result = tryRead(val);
    val = Endian::little(val);
    return result;
  }

  /**
   * Read a value from the cursor.
   *
   * @methodset Consumers
   *
   * This function does not perform bounds checking: there must be enough bytes
   * left in the cursor to populate T.
   *
   * This function only works with types that are bit-copyable: it calls memcpy
   * to reinterpret bits from the IOBuf as T.
   */
  template <class T>
  T read() {
    if (FOLLY_LIKELY(uintptr_t(crtPos_) + sizeof(T) <= uintptr_t(crtEnd_))) {
      T val = loadUnaligned<T>(data());
      crtPos_ += sizeof(T);
      return val;
    } else {
      return readSlow<T>();
    }
  }

  /**
   * Read a Big-Endian integral from the cursor.
   *
   * @methodset Consumers
   *
   * @see read
   */
  template <class T>
  T readBE() {
    return Endian::big(read<T>());
  }

  /**
   * Read a Little-Endian integral from the cursor.
   *
   * @methodset Consumers
   *
   * @see read
   */
  template <class T>
  T readLE() {
    return Endian::little(read<T>());
  }

  /**
   * Read a fixed-length string.
   *
   * @methodset Consumers
   *
   * The std::string-based APIs should probably be avoided unless you
   * ultimately want the data to live in an std::string. You're better off
   * using the pull() APIs to copy into a raw buffer otherwise.
   */
  std::string readFixedString(size_t len) {
    std::string str;
    str.reserve(len);
    if (FOLLY_LIKELY(length() >= len)) {
      str.append(reinterpret_cast<const char*>(data()), len);
      crtPos_ += len;
    } else {
      readFixedStringSlow(&str, len);
    }
    return str;
  }

  /**
   * Read a string of bytes until the given terminator character is seen.
   *
   * @methodset Consumers
   *
   * Raises an std::length_error if maxLength bytes have been processed
   * before the terminator is seen.
   *
   * See comments in readFixedString() about when it's appropriate to use this
   * vs. using pull().
   */
  std::string readTerminatedString(
      char termChar = '\0',
      size_t maxLength = std::numeric_limits<size_t>::max());

  /**
   * @overloadbrief Read bytes until the specified predicate returns true.
   *
   * @methodset Consumers
   *
   * The predicate will be called on each byte in turn, until it returns false
   * or until the end of the IOBuf chain is reached.
   *
   * Returns the result as a string.
   */
  template <typename Predicate>
  std::string readWhile(const Predicate& predicate);

  /**
   * This is a more generic version of readWhile(). It takes an arbitrary Output
   * object, and calls Output::append() with each chunk of matching data.
   */
  template <typename Predicate, typename Output>
  void readWhile(const Predicate& predicate, Output& out);

  /**
   * Skip bytes until the specified predicate returns true.
   *
   * @methodset Consumers
   *
   * The predicate will be called on each byte in turn, until it returns false
   * or until the end of the IOBuf chain is reached.
   */
  template <typename Predicate>
  void skipWhile(const Predicate& predicate);

  /**
   * Advance the cursor by at most len bytes.
   *
   * @methodset Modifiers
   */
  size_t skipAtMost(size_t len) {
    dcheckIntegrity();
    if (FOLLY_LIKELY(uintptr_t(crtPos_) + len < uintptr_t(crtEnd_))) {
      crtPos_ += len;
      return len;
    }
    return skipAtMostSlow(len);
  }

  /**
   * Advance the cursor by len bytes.
   *
   * @methodset Modifiers
   *
   * @throws out_of_range if there aren't enough bytes left in the cursor.
   */
  void skip(size_t len) {
    dcheckIntegrity();
    if (FOLLY_LIKELY(uintptr_t(crtPos_) + len < uintptr_t(crtEnd_))) {
      crtPos_ += len;
    } else {
      skipSlow(len);
    }
  }

  /**
   * Skip bytes in the current IOBuf without advancing to the next one.
   *
   * @methodset Modifiers
   *
   * @pre length() >= len
   */
  void skipNoAdvance(size_t len) {
    DCHECK_LE(len, length());
    crtPos_ += len;
  }

  /**
   * Retreat the cursor by at most len bytes.
   *
   * @methodset Modifiers
   */
  size_t retreatAtMost(size_t len) {
    dcheckIntegrity();
    if (len <= static_cast<size_t>(crtPos_ - crtBegin_)) {
      crtPos_ -= len;
      return len;
    }
    return retreatAtMostSlow(len);
  }

  /**
   * Retreat the cursor by at most len bytes.
   *
   * @methodset Modifiers
   *
   * @throws out_of_range if the cursor doesn't have enough bytes to retreat.
   */
  void retreat(size_t len) {
    dcheckIntegrity();
    if (len <= static_cast<size_t>(crtPos_ - crtBegin_)) {
      crtPos_ -= len;
    } else {
      retreatSlow(len);
    }
  }

  /**
   * Copies at most len bytes from the cursor.
   *
   * @methodset Consumers
   *
   * The cursor will advance by the number of bytes read.
   *
   * @param[out] buf The buffer into which the bytes are copied.
   * @returns The number of bytes copied.
   */
  size_t pullAtMost(void* buf, size_t len) {
    if (FOLLY_UNLIKELY(len == 0)) {
      return 0;
    }
    dcheckIntegrity();
    // Fast path: it all fits in one buffer.
    if (FOLLY_LIKELY(uintptr_t(crtPos_) + len <= uintptr_t(crtEnd_))) {
      memcpy(buf, data(), len);
      crtPos_ += len;
      return len;
    }
    return pullAtMostSlow(buf, len);
  }

  /**
   * Copies len bytes from the cursor.
   *
   * @methodset Consumers
   *
   * The cursor will advance by len.
   *
   * @param[out] buf The buffer into which the bytes are copied.
   * @throw out_of_range if there aren't enough bytes in the cursor.
   */
  void pull(void* buf, size_t len) {
    if (FOLLY_UNLIKELY(len == 0)) {
      return;
    }
    dcheckIntegrity();
    if (FOLLY_LIKELY(uintptr_t(crtPos_) + len <= uintptr_t(crtEnd_))) {
      memcpy(buf, data(), len);
      crtPos_ += len;
    } else {
      pullSlow(buf, len);
    }
  }

  /**
   * Return the available data in the current IOBuf.
   *
   * @methodset Accessors
   *
   * Unlike data(), peekBytes() will advance to the next IOBuf while length()==0
   * (though it can still return an empty range if there are no bytes left to
   * read in the whole IOBuf chain).
   *
   * If you want to gather more data from the chain into a contiguous region
   * (for hopefully zero-copy access), use gather() before peekBytes().
   */
  ByteRange peekBytes() {
    // Ensure that we're pointing to valid data
    size_t available = length();
    while (FOLLY_UNLIKELY(available == 0 && tryAdvanceBuffer())) {
      available = length();
    }
    return ByteRange{data(), available};
  }

  /**
   * Alternate version of peekBytes() that returns a std::pair
   * instead of a ByteRange.
   *
   * @methodset Accessors
   *
   * This function will eventually be deprecated.
   */
  std::pair<const uint8_t*, size_t> peek() {
    auto bytes = peekBytes();
    return std::make_pair(bytes.data(), bytes.size());
  }

  /**
   * Clone len bytes from this cursor into an IOBuf.
   *
   * @methodset Accessors
   *
   * @param[out] buf The IOBuf into which to place the cloned data.
   * @throws out_of_range if there aren't enough bytes in this cursor.
   */
  void clone(std::unique_ptr<folly::IOBuf>& buf, size_t len) {
    if (FOLLY_UNLIKELY(cloneAtMost(buf, len) != len)) {
      throw_exception<std::out_of_range>("underflow");
    }
  }

  void clone(folly::IOBuf& buf, size_t len) {
    if (FOLLY_UNLIKELY(cloneAtMost(buf, len) != len)) {
      throw_exception<std::out_of_range>("underflow");
    }
  }

  /**
   * Clone at most len bytes from this cursor into an IOBuf.
   *
   * @methodset Accessors
   *
   * @param[out] buf The IOBuf into which to place the cloned data.
   * @return The number of bytes actually cloned.
   */
  size_t cloneAtMost(folly::IOBuf& buf, size_t len) {
    // We might be at the end of buffer.
    advanceBufferIfEmpty();

    std::unique_ptr<folly::IOBuf> tmp;
    size_t copied = 0;
    for (int loopCount = 0; true; ++loopCount) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (FOLLY_LIKELY(available >= len)) {
        if (loopCount == 0) {
          crtBuf_->cloneOneInto(buf);
          buf.trimStart(crtPos_ - crtBegin_);
          buf.trimEnd(buf.length() - len);
        } else {
          tmp = crtBuf_->cloneOne();
          tmp->trimStart(crtPos_ - crtBegin_);
          tmp->trimEnd(tmp->length() - len);
          buf.prependChain(std::move(tmp));
        }

        crtPos_ += len;
        advanceBufferIfEmpty();
        return copied + len;
      }

      if (loopCount == 0) {
        crtBuf_->cloneOneInto(buf);
        buf.trimStart(crtPos_ - crtBegin_);
      } else {
        tmp = crtBuf_->cloneOne();
        tmp->trimStart(crtPos_ - crtBegin_);
        buf.prependChain(std::move(tmp));
      }

      copied += available;
      if (FOLLY_UNLIKELY(!tryAdvanceBuffer())) {
        return copied;
      }
      len -= available;
    }
  }

  size_t cloneAtMost(std::unique_ptr<folly::IOBuf>& buf, size_t len) {
    if (!buf) {
      buf = std::make_unique<folly::IOBuf>();
    }
    return cloneAtMost(*buf, len);
  }

  /**
   * Return the distance between two cursors.
   */
  size_t operator-(const CursorBase& other) const {
    BufType* otherBuf = other.crtBuf_;
    size_t len = 0;

    if (otherBuf != crtBuf_) {
      if (other.remainingLen_ == 0) {
        len += otherBuf->tail() - other.crtPos_;
      } else {
        len += other.crtEnd_ - other.crtPos_;
      }

      for (otherBuf = otherBuf->next();
           otherBuf != crtBuf_ && otherBuf != other.buffer_;
           otherBuf = otherBuf->next()) {
        len += otherBuf->length();
      }

      if (otherBuf == other.buffer_) {
        throw_exception<std::out_of_range>("wrap-around");
      }

      len += crtPos_ - crtBegin_;
    } else {
      if (crtPos_ < other.crtPos_) {
        throw_exception<std::out_of_range>("underflow");
      }

      len += crtPos_ - other.crtPos_;
    }

    return len;
  }

  /**
   * Return the distance from the given IOBuf to the this cursor.
   */
  size_t operator-(const BufType* buf) const {
    size_t len = 0;

    const BufType* curBuf = buf;
    while (curBuf != crtBuf_) {
      len += curBuf->length();
      curBuf = curBuf->next();
      if (curBuf == buf || curBuf == buffer_) {
        throw_exception<std::out_of_range>("wrap-around");
      }
    }

    len += crtPos_ - crtBegin_;
    return len;
  }

  /**
   * Check if this cursor has a size limit imposed on it.
   *
   * @methodset Configuration
   */
  bool isBounded() const {
    return remainingLen_ != std::numeric_limits<size_t>::max();
  }

 protected:
  void dcheckIntegrity() const {
    DCHECK(crtBegin_ <= crtPos_ && crtPos_ <= crtEnd_);
    DCHECK(crtBuf_ == nullptr || crtBegin_ == crtBuf_->data());
    DCHECK(
        crtBuf_ == nullptr ||
        (std::size_t)(crtEnd_ - crtBegin_) <= crtBuf_->length());
  }

  ~CursorBase() = default;

  BufType* head() { return buffer_; }

  bool tryAdvanceBuffer() {
    BufType* nextBuf = crtBuf_->next();
    if (FOLLY_UNLIKELY(nextBuf == buffer_) || remainingLen_ == 0) {
      crtPos_ = crtEnd_;
      return false;
    }

    absolutePos_ += crtEnd_ - crtBegin_;
    crtBuf_ = nextBuf;
    crtPos_ = crtBegin_ = crtBuf_->data();
    crtEnd_ = crtBuf_->tail();
    if (isBounded()) {
      if (uintptr_t(crtPos_) + remainingLen_ < uintptr_t(crtEnd_)) {
        crtEnd_ = crtPos_ + remainingLen_;
      }
      remainingLen_ -= crtEnd_ - crtPos_;
    }
    derived().advanceDone();
    return true;
  }

  bool tryRetreatBuffer() {
    if (FOLLY_UNLIKELY(crtBuf_ == buffer_)) {
      crtPos_ = crtBegin_;
      return false;
    }
    if (isBounded()) {
      remainingLen_ += crtEnd_ - crtBegin_;
    }
    crtBuf_ = crtBuf_->prev();
    crtBegin_ = crtBuf_->data();
    crtPos_ = crtEnd_ = crtBuf_->tail();
    absolutePos_ -= crtEnd_ - crtBegin_;
    derived().advanceDone();
    return true;
  }

  void advanceBufferIfEmpty() {
    dcheckIntegrity();
    if (crtPos_ == crtEnd_) {
      tryAdvanceBuffer();
    }
  }

  BufType* crtBuf_;
  BufType* buffer_;
  const uint8_t* crtBegin_{nullptr};
  const uint8_t* crtEnd_{nullptr};
  const uint8_t* crtPos_{nullptr};
  size_t absolutePos_{0};
  // For bounded Cursor, remainingLen_ is the remaining number of data bytes
  // in subsequent IOBufs in the chain. For unbounded Cursor, remainingLen_
  // is set to the max of size_t
  size_t remainingLen_{std::numeric_limits<size_t>::max()};

 private:
  Derived& derived() { return static_cast<Derived&>(*this); }

  Derived const& derived() const { return static_cast<const Derived&>(*this); }

  template <class T>
  FOLLY_NOINLINE T readSlow() {
    T val;
    pullSlow(&val, sizeof(T));
    return val;
  }

  FOLLY_NOINLINE void readFixedStringSlow(std::string* str, size_t len) {
    for (size_t available; (available = length()) < len;) {
      str->append(reinterpret_cast<const char*>(data()), available);
      if (FOLLY_UNLIKELY(!tryAdvanceBuffer())) {
        throw_exception<std::out_of_range>("string underflow");
      }
      len -= available;
    }
    str->append(reinterpret_cast<const char*>(data()), len);
    crtPos_ += len;
    advanceBufferIfEmpty();
  }

  FOLLY_NOINLINE size_t pullAtMostSlow(void* buf, size_t len) {
    uint8_t* p = reinterpret_cast<uint8_t*>(buf);
    size_t copied = 0;
    for (size_t available; (available = length()) < len;) {
      if (available > 0) {
        // Don't try to copy from 0-length buffers, since they could have
        // a null data() pointer.
        memcpy(p, data(), available);
        copied += available;
      }
      if (FOLLY_UNLIKELY(!tryAdvanceBuffer())) {
        return copied;
      }
      p += available;
      len -= available;
    }
    if (len > 0) {
      // Don't try to copy from 0-length buffers, since they could have
      // a null data() pointer.
      memcpy(p, data(), len);
      crtPos_ += len;
    }
    advanceBufferIfEmpty();
    return copied + len;
  }

  FOLLY_NOINLINE void pullSlow(void* buf, size_t len) {
    if (FOLLY_UNLIKELY(pullAtMostSlow(buf, len) != len)) {
      throw_exception<std::out_of_range>("underflow");
    }
  }

  FOLLY_NOINLINE size_t skipAtMostSlow(size_t len) {
    size_t skipped = 0;
    for (size_t available; (available = length()) < len;) {
      skipped += available;
      if (FOLLY_UNLIKELY(!tryAdvanceBuffer())) {
        return skipped;
      }
      len -= available;
    }
    crtPos_ += len;
    advanceBufferIfEmpty();
    return skipped + len;
  }

  FOLLY_NOINLINE void skipSlow(size_t len) {
    if (FOLLY_UNLIKELY(skipAtMostSlow(len) != len)) {
      throw_exception<std::out_of_range>("underflow");
    }
  }

  FOLLY_NOINLINE size_t retreatAtMostSlow(size_t len) {
    size_t retreated = 0;
    for (size_t available; (available = crtPos_ - crtBegin_) < len;) {
      retreated += available;
      if (FOLLY_UNLIKELY(!tryRetreatBuffer())) {
        return retreated;
      }
      len -= available;
    }
    crtPos_ -= len;
    return retreated + len;
  }

  FOLLY_NOINLINE void retreatSlow(size_t len) {
    if (FOLLY_UNLIKELY(retreatAtMostSlow(len) != len)) {
      throw_exception<std::out_of_range>("underflow");
    }
  }

  void advanceDone() {}
};

} // namespace detail

class Cursor : public detail::CursorBase<Cursor, const IOBuf> {
 public:
  explicit Cursor(const IOBuf* buf)
      : detail::CursorBase<Cursor, const IOBuf>(buf) {}

  explicit Cursor(const IOBuf* buf, size_t len)
      : detail::CursorBase<Cursor, const IOBuf>(buf, len) {}

  template <class OtherDerived, class OtherBuf>
  explicit Cursor(const detail::CursorBase<OtherDerived, OtherBuf>& cursor)
      : detail::CursorBase<Cursor, const IOBuf>(cursor) {}

  template <class OtherDerived, class OtherBuf>
  Cursor(const detail::CursorBase<OtherDerived, OtherBuf>& cursor, size_t len)
      : detail::CursorBase<Cursor, const IOBuf>(cursor, len) {}
};

namespace detail {

template <class Derived>
class Writable {
 public:
  template <class T>
  typename std::enable_if<std::is_arithmetic<T>::value>::type write(
      T value, size_t n = sizeof(T)) {
    assert(n <= sizeof(T));
    const uint8_t* u8 = reinterpret_cast<const uint8_t*>(&value);
    Derived* d = static_cast<Derived*>(this);
    d->push(u8, n);
  }

  template <class T>
  void writeBE(T value) {
    Derived* d = static_cast<Derived*>(this);
    d->write(Endian::big(value));
  }

  template <class T>
  void writeLE(T value) {
    Derived* d = static_cast<Derived*>(this);
    d->write(Endian::little(value));
  }

  void push(const uint8_t* buf, size_t len) {
    Derived* d = static_cast<Derived*>(this);
    if (d->pushAtMost(buf, len) != len) {
      throw_exception<std::out_of_range>("overflow");
    }
  }

  void push(ByteRange buf) {
    if (this->pushAtMost(buf) != buf.size()) {
      throw_exception<std::out_of_range>("overflow");
    }
  }

  size_t pushAtMost(ByteRange buf) {
    Derived* d = static_cast<Derived*>(this);
    return d->pushAtMost(buf.data(), buf.size());
  }

  /**
   * push len bytes of data from input cursor, data could be in an IOBuf chain.
   * If input cursor contains less than len bytes, or this cursor has less than
   * len bytes writable space, an out_of_range exception will be thrown.
   */
  void push(Cursor cursor, size_t len) {
    if (this->pushAtMost(cursor, len) != len) {
      throw_exception<std::out_of_range>("overflow");
    }
  }

  size_t pushAtMost(Cursor cursor, size_t len) {
    size_t written = 0;
    for (;;) {
      auto currentBuffer = cursor.peekBytes();
      const uint8_t* crtData = currentBuffer.data();
      size_t available = currentBuffer.size();
      if (available == 0) {
        // end of buffer chain
        return written;
      }
      // all data is in current buffer
      if (available >= len) {
        this->push(crtData, len);
        cursor.skip(len);
        return written + len;
      }

      // write the whole current IOBuf
      this->push(crtData, available);
      cursor.skip(available);
      written += available;
      len -= available;
    }
  }
};

} // namespace detail

enum class CursorAccess { PRIVATE, UNSHARE };

template <CursorAccess access>
class RWCursor : public detail::CursorBase<RWCursor<access>, IOBuf>,
                 public detail::Writable<RWCursor<access>> {
  friend class detail::CursorBase<RWCursor<access>, IOBuf>;

 public:
  explicit RWCursor(IOBuf* buf)
      : detail::CursorBase<RWCursor<access>, IOBuf>(buf), maybeShared_(true) {}

  explicit RWCursor(IOBufQueue& queue)
      : RWCursor((queue.flushCache(), queue.head_.get())) {}

  // Efficient way to advance to position cursor to the end of the queue,
  // using cached length instead of a walk via advanceToEnd().
  struct AtEnd {};
  RWCursor(IOBufQueue& queue, AtEnd) : RWCursor(queue) {
    if (!queue.options().cacheChainLength) {
      this->advanceToEnd();
    } else {
      this->crtBuf_ = this->buffer_->prev();
      this->crtBegin_ = this->crtBuf_->data();
      this->crtEnd_ = this->crtBuf_->tail();
      this->crtPos_ = this->crtEnd_;
      this->absolutePos_ =
          queue.chainLength() - (this->crtPos_ - this->crtBegin_);
      DCHECK_EQ(this->getCurrentPosition(), queue.chainLength());
    }
  }

  template <class OtherDerived, class OtherBuf>
  explicit RWCursor(const detail::CursorBase<OtherDerived, OtherBuf>& cursor)
      : detail::CursorBase<RWCursor<access>, IOBuf>(cursor),
        maybeShared_(true) {
    CHECK(!cursor.isBounded())
        << "Creating RWCursor from bounded Cursor is not allowed";
  }
  /**
   * Gather at least n bytes contiguously into the current buffer,
   * by coalescing subsequent buffers from the chain as necessary.
   */
  void gather(size_t n) {
    // Forbid attempts to gather beyond the end of this IOBuf chain.
    // Otherwise we could try to coalesce the head of the chain and end up
    // accidentally freeing it, invalidating the pointer owned by external
    // code.
    //
    // If crtBuf_ == head() then IOBuf::gather() will perform all necessary
    // checking.  We only have to perform an explicit check here when calling
    // gather() on a non-head element.
    if (this->crtBuf_ != this->head() && this->totalLength() < n) {
      throw std::overflow_error("cannot gather() past the end of the chain");
    }
    size_t offset = this->crtPos_ - this->crtBegin_;
    this->crtBuf_->gather(offset + n);
    this->crtBegin_ = this->crtBuf_->data();
    this->crtEnd_ = this->crtBuf_->tail();
    this->crtPos_ = this->crtBegin_ + offset;
  }
  void gatherAtMost(size_t n) {
    this->dcheckIntegrity();
    size_t size = std::min(n, this->totalLength());
    size_t offset = this->crtPos_ - this->crtBegin_;
    this->crtBuf_->gather(offset + size);
    this->crtBegin_ = this->crtBuf_->data();
    this->crtEnd_ = this->crtBuf_->tail();
    this->crtPos_ = this->crtBegin_ + offset;
  }

  using detail::Writable<RWCursor<access>>::pushAtMost;
  size_t pushAtMost(const uint8_t* buf, size_t len) {
    // We have to explicitly check for an input length of 0.
    // We support buf being nullptr in this case, but we need to avoid calling
    // memcpy() with a null source pointer, since that is undefined behavior
    // even if the length is 0.
    if (len == 0) {
      return 0;
    }

    size_t copied = 0;
    for (;;) {
      // Fast path: the current buffer is big enough.
      size_t available = this->length();
      if (FOLLY_LIKELY(available >= len)) {
        if (access == CursorAccess::UNSHARE) {
          maybeUnshare();
        }
        memcpy(writableData(), buf, len);
        this->crtPos_ += len;
        return copied + len;
      }

      if (access == CursorAccess::UNSHARE) {
        maybeUnshare();
      }
      memcpy(writableData(), buf, available);
      copied += available;
      if (FOLLY_UNLIKELY(!this->tryAdvanceBuffer())) {
        return copied;
      }
      buf += available;
      len -= available;
    }
  }

  void insert(std::unique_ptr<folly::IOBuf> buf) {
    this->dcheckIntegrity();
    this->absolutePos_ += buf->computeChainDataLength();
    if (this->crtPos_ == this->crtBegin_ && this->crtBuf_ != this->buffer_) {
      // Can just prepend
      this->crtBuf_->prependChain(std::move(buf));
    } else {
      IOBuf* nextBuf;
      std::unique_ptr<folly::IOBuf> remaining;
      if (this->crtPos_ != this->crtEnd_) {
        // Need to split current IOBuf in two.
        remaining = this->crtBuf_->cloneOne();
        remaining->trimStart(this->crtPos_ - this->crtBegin_);
        nextBuf = remaining.get();
        buf->prependChain(std::move(remaining));
      } else {
        // Can just append
        nextBuf = this->crtBuf_->next();
      }
      this->crtBuf_->trimEnd(this->length());
      this->absolutePos_ += this->crtPos_ - this->crtBegin_;
      this->crtBuf_->insertAfterThisOne(std::move(buf));

      if (nextBuf == this->buffer_) {
        // We've just appended to the end of the buffer, so advance to the end.
        this->crtBuf_ = this->buffer_->prev();
        this->crtBegin_ = this->crtBuf_->data();
        this->crtPos_ = this->crtEnd_ = this->crtBuf_->tail();
        // This has already been accounted for, so remove it.
        this->absolutePos_ -= this->crtEnd_ - this->crtBegin_;
      } else {
        // Jump past the new links
        this->crtBuf_ = nextBuf;
        this->crtPos_ = this->crtBegin_ = this->crtBuf_->data();
        this->crtEnd_ = this->crtBuf_->tail();
      }
    }
  }

  uint8_t* writableData() {
    this->dcheckIntegrity();
    return this->crtBuf_->writableData() + (this->crtPos_ - this->crtBegin_);
  }

 private:
  void maybeUnshare() {
    if (FOLLY_UNLIKELY(maybeShared_)) {
      size_t offset = this->crtPos_ - this->crtBegin_;
      this->crtBuf_->unshareOne();
      this->crtBegin_ = this->crtBuf_->data();
      this->crtEnd_ = this->crtBuf_->tail();
      this->crtPos_ = this->crtBegin_ + offset;
      maybeShared_ = false;
    }
  }

  void advanceDone() { maybeShared_ = true; }

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
  Appender(IOBuf* buf, std::size_t growth)
      : buffer_(buf), crtBuf_(buf->prev()), growth_(growth) {}

  uint8_t* writableData() { return crtBuf_->writableTail(); }

  size_t length() const { return crtBuf_->tailroom(); }

  /**
   * Mark n bytes (must be <= length()) as appended, as per the
   * IOBuf::append() method.
   */
  void append(size_t n) { crtBuf_->append(n); }

  /**
   * Ensure at least n contiguous bytes available to write.
   * Postcondition: length() >= n.
   */
  void ensure(std::size_t n) {
    if (FOLLY_LIKELY(length() >= n)) {
      return;
    }

    // Waste the rest of the current buffer and allocate a new one.
    // Don't make it too small, either.
    if (growth_ == 0) {
      throw_exception<std::out_of_range>("can't grow buffer chain");
    }

    n = std::max(n, growth_);
    buffer_->prependChain(IOBuf::create(n));
    crtBuf_ = buffer_->prev();
  }

  using detail::Writable<Appender>::pushAtMost;
  size_t pushAtMost(const uint8_t* buf, size_t len) {
    // We have to explicitly check for an input length of 0.
    // We support buf being nullptr in this case, but we need to avoid calling
    // memcpy() with a null source pointer, since that is undefined behavior
    // even if the length is 0.
    if (len == 0) {
      return 0;
    }

    // If the length of this buffer is 0 try growing it.
    // Otherwise on the first iteration of the following loop memcpy is called
    // with a null source pointer.
    if (FOLLY_UNLIKELY(length() == 0 && !tryGrowChain())) {
      return 0;
    }

    size_t copied = 0;
    for (;;) {
      // Fast path: it all fits in one buffer.
      size_t available = length();
      if (FOLLY_LIKELY(available >= len)) {
        memcpy(writableData(), buf, len);
        append(len);
        return copied + len;
      }

      memcpy(writableData(), buf, available);
      append(available);
      copied += available;
      if (FOLLY_UNLIKELY(!tryGrowChain())) {
        return copied;
      }
      buf += available;
      len -= available;
    }
  }

  /**
   * Append to the end of this buffer, using a printf() style
   * format specifier.
   *
   * Note that folly/Format.h provides nicer and more type-safe mechanisms
   * for formatting strings, which should generally be preferred over
   * printf-style formatting.  Appender objects can be used directly as an
   * output argument for Formatter objects.  For example:
   *
   *   Appender app(&iobuf);
   *   format("{} {}", "hello", "world")(app);
   *
   * However, printf-style strings are still needed when dealing with existing
   * third-party code in some cases.
   *
   * This will always add a nul-terminating character after the end
   * of the output.  However, the buffer data length will only be updated to
   * include the data itself.  The nul terminator will be the first byte in the
   * buffer tailroom.
   *
   * This method may throw exceptions on error.
   */
  void printf(FOLLY_PRINTF_FORMAT const char* fmt, ...)
      FOLLY_PRINTF_FORMAT_ATTR(2, 3);

  void vprintf(const char* fmt, va_list ap);

  /**
   * Calling an Appender object with a StringPiece will append the string
   * piece.  This allows Appender objects to be used directly with
   * Formatter.
   */
  void operator()(StringPiece sp) { push(ByteRange(sp)); }

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
  std::size_t growth_;
};

class QueueAppender : public detail::Writable<QueueAppender> {
 public:
  /**
   * Create an Appender that writes to a IOBufQueue.  When we allocate
   * space in the queue, we grow no more than growth bytes at once
   * (unless you call ensure() with a bigger value yourself).
   */
  QueueAppender(IOBufQueue* queue, std::size_t growth)
      : queueCache_(queue), growth_(growth) {}

  void reset(IOBufQueue* queue, std::size_t growth) {
    queueCache_.reset(queue);
    growth_ = growth;
  }

  uint8_t* writableData() { return queueCache_.writableData(); }

  size_t length() { return queueCache_.length(); }

  void append(size_t n) { queueCache_.append(n); }

  // Ensure at least n contiguous; can go above growth_, throws if
  // not enough room.
  void ensure(size_t n) {
    if (length() < n) {
      ensureSlow(n);
    }
  }

  template <class T>
  typename std::enable_if<std::is_arithmetic<T>::value>::type write(
      T value, size_t n = sizeof(T)) {
    // We can't fail.
    assert(n <= sizeof(T));
    if (length() >= sizeof(T)) {
      storeUnaligned(queueCache_.writableData(), value);
      queueCache_.appendUnsafe(n);
    } else {
      writeSlow<T>(value, n);
    }
  }

  using detail::Writable<QueueAppender>::pushAtMost;
  size_t pushAtMost(const uint8_t* buf, size_t len) {
    // Fill the current buffer
    const size_t copyLength = std::min(len, length());
    if (copyLength != 0) {
      memcpy(writableData(), buf, copyLength);
      queueCache_.appendUnsafe(copyLength);
      buf += copyLength;
    }
    size_t remaining = len - copyLength;
    // Allocate more buffers as necessary
    while (remaining != 0) {
      auto p = queueCache_.queue()->preallocate(
          std::min(remaining, growth_), growth_, remaining);
      memcpy(p.first, buf, p.second);
      queueCache_.queue()->postallocate(p.second);
      buf += p.second;
      remaining -= p.second;
    }
    return len;
  }

  void insert(std::unique_ptr<folly::IOBuf> buf) {
    if (buf) {
      queueCache_.queue()->append(
          std::move(buf), /* pack */ true, /* allowTailReuse */ true);
    }
  }

  void insert(const folly::IOBuf& buf) {
    queueCache_.queue()->append(
        buf, /* pack */ true, /* allowTailReuse */ true);
  }

  template <CursorAccess access>
  explicit operator RWCursor<access>() {
    return RWCursor<access>(*queueCache_.queue());
  }

  template <CursorAccess access>
  RWCursor<access> tail(size_t n) {
    RWCursor<access> result(
        *queueCache_.queue(), typename RWCursor<access>::AtEnd{});
    result -= n;
    return result;
  }

  void trimEnd(size_t n) { queueCache_.queue()->trimEnd(n); }

 private:
  folly::IOBufQueue::WritableRangeCache queueCache_{nullptr};
  size_t growth_{0};

  FOLLY_NOINLINE void ensureSlow(size_t n) {
    queueCache_.queue()->preallocate(n, growth_);
    queueCache_.fillCache();
  }

  template <class T>
  typename std::enable_if<std::is_arithmetic<T>::value>::type FOLLY_NOINLINE
  writeSlow(T value, size_t n = sizeof(T)) {
    assert(n <= sizeof(T));
    queueCache_.queue()->preallocate(sizeof(T), growth_);
    queueCache_.fillCache();

    storeUnaligned(queueCache_.writableData(), value);
    queueCache_.appendUnsafe(n);
  }
};

} // namespace io
} // namespace folly

#include <folly/io/Cursor-inl.h>
