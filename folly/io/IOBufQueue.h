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

#include <stdexcept>
#include <string>

#include <folly/ScopeGuard.h>
#include <folly/io/IOBuf.h>

namespace folly {

namespace io {
enum class CursorAccess;
template <CursorAccess>
class RWCursor;
} // namespace io

/**
 * An IOBufQueue encapsulates a chain of IOBufs and provides
 * convenience functions to append data to the back of the chain
 * and remove data from the front.
 *
 * You may also prepend data into the headroom of the first buffer in the
 * chain, if any.
 */
class IOBufQueue {
 private:
  template <io::CursorAccess>
  friend class io::RWCursor;

  /**
   * This guard should be taken by any method that intends to do any changes
   * to in data_ (e.g. appending to it).
   *
   * It flushes the writable tail cache and refills it on destruction.
   */
  auto updateGuard() {
    flushCache();
    return folly::makeGuard([this] { updateWritableTailCache(); });
  }

  struct WritableRangeCacheData {
    std::pair<uint8_t*, uint8_t*> cachedRange;
    bool attached{false};

    WritableRangeCacheData() = default;

    WritableRangeCacheData(WritableRangeCacheData&& other)
        : cachedRange(other.cachedRange), attached(other.attached) {
      other.cachedRange = {nullptr, nullptr};
      other.attached = false;
    }
    WritableRangeCacheData& operator=(WritableRangeCacheData&& other) {
      cachedRange = other.cachedRange;
      attached = other.attached;

      other.cachedRange = {nullptr, nullptr};
      other.attached = false;

      return *this;
    }

    WritableRangeCacheData(const WritableRangeCacheData&) = delete;
    WritableRangeCacheData& operator=(const WritableRangeCacheData&) = delete;
  };

 public:
  struct Options {
    Options() : cacheChainLength(false) {}
    bool cacheChainLength;
  };

  /**
   * Commonly used Options, currently the only possible value other than
   * the default.
   */
  static Options cacheChainLength() {
    Options options;
    options.cacheChainLength = true;
    return options;
  }

  /**
   * WritableRangeCache represents a cache of current writable tail and provides
   * cheap and simple interface to append to it that avoids paying the cost of
   * preallocate/postallocate pair (i.e. indirections and checks).
   *
   * The cache is flushed on destruction/copy/move and on non-const accesses to
   * the underlying IOBufQueue.
   *
   * Note: there can be only one active cache for a given IOBufQueue, i.e. when
   *       you fill a cache object it automatically invalidates other
   *       cache (if any).
   */
  class WritableRangeCache {
   public:
    explicit WritableRangeCache(folly::IOBufQueue* q = nullptr) : queue_(q) {
      if (queue_) {
        fillCache();
      }
    }

    /**
     * Move constructor/assignment can move the cached range, but must update
     * the reference in IOBufQueue.
     */
    WritableRangeCache(WritableRangeCache&& other)
        : data_(std::move(other.data_)), queue_(other.queue_) {
      if (data_.attached) {
        queue_->updateCacheRef(data_);
      }
    }
    WritableRangeCache& operator=(WritableRangeCache&& other) {
      if (data_.attached) {
        queue_->clearWritableRangeCache();
      }

      data_ = std::move(other.data_);
      queue_ = other.queue_;

      if (data_.attached) {
        queue_->updateCacheRef(data_);
      }

      return *this;
    }

    /**
     * Copy constructor/assignment cannot copy the cached range.
     */
    WritableRangeCache(const WritableRangeCache& other)
        : queue_(other.queue_) {}
    WritableRangeCache& operator=(const WritableRangeCache& other) {
      if (data_.attached) {
        queue_->clearWritableRangeCache();
      }

      queue_ = other.queue_;

      return *this;
    }

    ~WritableRangeCache() {
      if (data_.attached) {
        queue_->clearWritableRangeCache();
      }
    }

    /**
     * Reset the underlying IOBufQueue, will flush current cache if present.
     */
    void reset(IOBufQueue* q) {
      if (data_.attached) {
        queue_->clearWritableRangeCache();
      }

      queue_ = q;

      if (queue_) {
        fillCache();
      }
    }

    /**
     * Get a pointer to the underlying IOBufQueue object.
     */
    IOBufQueue* queue() { return queue_; }

    /**
     * Return a pointer to the start of cached writable tail.
     *
     * Note: doesn't populate cache.
     */
    uint8_t* writableData() {
      dcheckIntegrity();
      return data_.cachedRange.first;
    }

    /**
     * Return a length of cached writable tail.
     *
     * Note: doesn't populate cache.
     */
    size_t length() {
      dcheckIntegrity();
      return data_.cachedRange.second - data_.cachedRange.first;
    }

    /**
     * Mark n bytes as occupied (e.g. postallocate).
     */
    void append(size_t n) {
      dcheckIntegrity();
      // This can happen only if somebody is misusing the interface.
      // E.g. calling append after touching IOBufQueue or without checking
      // the length().
      if (FOLLY_LIKELY(data_.cachedRange.first != nullptr)) {
        DCHECK_LE(n, length());
        data_.cachedRange.first += n;
      } else {
        appendSlow(n);
      }
    }

    /**
     * Same as append(n), but avoids checking if there is a cache.
     * The caller must guarantee that the cache is set (e.g. the caller just
     * called fillCache or checked that it's not empty).
     */
    void appendUnsafe(size_t n) { data_.cachedRange.first += n; }

    /**
     * Fill the cache of writable tail from the underlying IOBufQueue.
     */
    void fillCache() { queue_->fillWritableRangeCache(data_); }

   private:
    WritableRangeCacheData data_;
    IOBufQueue* queue_;

    FOLLY_NOINLINE void appendSlow(size_t n) { queue_->postallocate(n); }

    void dcheckIntegrity() {
      // Tail start should always be less than tail end.
      DCHECK_LE(
          (void*)data_.cachedRange.first, (void*)data_.cachedRange.second);
      DCHECK(
          data_.cachedRange.first != nullptr ||
          data_.cachedRange.second == nullptr);

      // Cached range should be always empty if the cache is not attached.
      DCHECK(
          data_.attached ||
          (data_.cachedRange.first == nullptr &&
           data_.cachedRange.second == nullptr));

      // We cannot be in attached state if the queue_ is not set.
      DCHECK(queue_ != nullptr || !data_.attached);

      // If we're attached and the cache is not empty, then it should coincide
      // with the tail buffer.
      DCHECK(
          !data_.attached || data_.cachedRange.first == nullptr ||
          (queue_->head_ != nullptr &&
           data_.cachedRange.first >= queue_->head_->prev()->writableTail() &&
           data_.cachedRange.second ==
               queue_->head_->prev()->writableTail() +
                   queue_->head_->prev()->tailroom()));
    }
  };

  explicit IOBufQueue(const Options& options = Options());
  ~IOBufQueue();

  /**
   * Return a space to prepend bytes and the amount of headroom available.
   */
  std::pair<void*, std::size_t> headroom();

  /**
   * Indicate that n bytes from the headroom have been used.
   */
  void markPrepended(std::size_t n);

  /**
   * Prepend an existing range; throws std::overflow_error if not enough
   * room.
   */
  void prepend(const void* buf, std::size_t n);

  /**
   * Add a buffer or buffer chain to the end of this queue. The
   * queue takes ownership of buf.
   *
   * If pack is true, we try to reduce wastage at the end of this queue
   * by copying some data from the first buffers in the buf chain (and
   * releasing the buffers), if possible.  If pack is false, we leave
   * the chain topology unchanged.
   *
   * If allowTailReuse is true, the current writable tail is reappended at the
   * end of the chain when possible and beneficial.
   */
  void append(
      std::unique_ptr<folly::IOBuf>&& buf,
      bool pack = false,
      bool allowTailReuse = false);
  void append(
      const folly::IOBuf& buf, bool pack = false, bool allowTailReuse = false);

  /**
   * Add a queue to the end of this queue. The queue takes ownership of
   * all buffers from the other queue.
   */
  void append(
      IOBufQueue& other, bool pack = false, bool allowTailReuse = false);
  void append(
      IOBufQueue&& other, bool pack = false, bool allowTailReuse = false) {
    append(other, pack, allowTailReuse);
  }

  /**
   * Copy len bytes, starting at buf, to the end of this queue.
   * The caller retains ownership of the source data.
   */
  void append(const void* buf, size_t len);

  /**
   * Copy a string to the end of this queue.
   * The caller retains ownership of the source data.
   */
  void append(StringPiece sp) { append(sp.data(), sp.size()); }

  /**
   * Append a chain of IOBuf objects that point to consecutive regions
   * within buf.
   *
   * Just like IOBuf::wrapBuffer, this should only be used when the caller
   * knows ahead of time and can ensure that all IOBuf objects that will point
   * to this buffer will be destroyed before the buffer itself is destroyed;
   * all other caveats from wrapBuffer also apply.
   *
   * Every buffer except for the last will wrap exactly blockSize bytes.
   * Importantly, this method may be used to wrap buffers larger than 4GB.
   */
  void wrapBuffer(
      const void* buf,
      size_t len,
      std::size_t blockSize = (1U << 31)); // default block size: 2GB

  /**
   * Obtain a writable block of contiguous bytes at the end of this
   * queue, allocating more space if necessary.  The amount of space
   * reserved will be at least min.  If min contiguous space is not
   * available at the end of the queue, and IOBuf with size newAllocationSize
   * is appended to the chain and returned.  The actual available space
   * may be larger than newAllocationSize, but will be truncated to max,
   * if specified.
   *
   * If the caller subsequently writes anything into the returned space,
   * it must call the postallocate() method.
   *
   * @return The starting address of the block and the length in bytes.
   *
   * @note The point of the preallocate()/postallocate() mechanism is
   *       to support I/O APIs such as AsyncSocket::ReadCallback that
   *       request a buffer from the application and then, in a later
   *       callback, tell the application how much of the buffer they
   *       have filled with data.
   */
  std::pair<void*, std::size_t> preallocate(
      std::size_t min,
      std::size_t newAllocationSize,
      std::size_t max = std::numeric_limits<std::size_t>::max()) {
    dcheckCacheIntegrity();

    if (FOLLY_LIKELY(writableTail() != nullptr && tailroom() >= min)) {
      return std::make_pair(
          writableTail(), std::min<std::size_t>(max, tailroom()));
    }

    return preallocateSlow(min, newAllocationSize, max);
  }

  /**
   * Tell the queue that the caller has written data into the first n
   * bytes provided by the previous preallocate() call.
   *
   * @note n should be less than or equal to the size returned by
   *       preallocate().  If n is zero, the caller may skip the call
   *       to postallocate().  If n is nonzero, the caller must not
   *       invoke any other non-const methods on this IOBufQueue between
   *       the call to preallocate and the call to postallocate().
   */
  void postallocate(std::size_t n) {
    dcheckCacheIntegrity();
    DCHECK_LE(
        (void*)(cachePtr_->cachedRange.first + n),
        (void*)cachePtr_->cachedRange.second);
    cachePtr_->cachedRange.first += n;
  }

  /**
   * Obtain a writable block of n contiguous bytes, allocating more space
   * if necessary, and mark it as used.  The caller can fill it later.
   */
  void* allocate(std::size_t n) {
    void* p = preallocate(n, n).first;
    postallocate(n);
    return p;
  }

  void* writableTail() const {
    dcheckCacheIntegrity();
    return cachePtr_->cachedRange.first;
  }

  size_t tailroom() const {
    dcheckCacheIntegrity();
    return cachePtr_->cachedRange.second - cachePtr_->cachedRange.first;
  }

  /**
   * Split off the first n bytes of the queue into a separate IOBuf chain,
   * and transfer ownership of the new chain to the caller.  The IOBufQueue
   * retains ownership of everything after the split point.
   *
   * @warning If the split point lies in the middle of some IOBuf within
   *          the chain, this function may, as an implementation detail,
   *          clone that IOBuf.
   *
   * @throws std::underflow_error if n exceeds the number of bytes
   *         in the queue.
   */
  std::unique_ptr<folly::IOBuf> split(size_t n) { return split(n, true); }

  /**
   * Similar to split, but will return the entire queue instead of throwing
   * if n exceeds the number of bytes in the queue.
   */
  std::unique_ptr<folly::IOBuf> splitAtMost(size_t n) {
    return split(n, false);
  }

  /**
   * Similar to IOBuf::trimStart, but works on the whole queue.  Will
   * pop off buffers that have been completely trimmed.
   */
  void trimStart(size_t amount);

  /**
   * Similar to trimStart, but will trim at most amount bytes and returns
   * the number of bytes trimmed.
   */
  size_t trimStartAtMost(size_t amount);

  /**
   * Similar to IOBuf::trimEnd, but works on the whole queue.  Will
   * pop off buffers that have been completely trimmed.
   */
  void trimEnd(size_t amount);

  /**
   * Similar to trimEnd, but will trim at most amount bytes and returns
   * the number of bytes trimmed.
   */
  size_t trimEndAtMost(size_t amount);

  /**
   * Transfer ownership of the queue's entire IOBuf chain to the caller.
   */
  std::unique_ptr<folly::IOBuf> move() {
    auto guard = updateGuard();
    std::unique_ptr<folly::IOBuf> res = std::move(head_);
    chainLength_ = 0;
    return res;
  }

  folly::IOBuf moveAsValue() { return std::move(*move()); }

  /**
   * Access the front IOBuf.
   *
   * Note: caller will see the current state of the chain, but may not see
   *       future updates immediately, due to the presence of a tail cache.
   * Note: the caller may potentially clone the chain, thus marking all buffers
   *       as shared. We may still continue writing to the tail of the last
   *       IOBuf without checking if it's shared, but this is fine, since the
   *       cloned IOBufs won't reference that data.
   */
  const folly::IOBuf* front() const {
    flushCache();
    return head_.get();
  }

  /**
   * returns the first IOBuf in the chain and removes it from the chain
   *
   * @return first IOBuf in the chain or nullptr if none.
   */
  std::unique_ptr<folly::IOBuf> pop_front();

  /**
   * Total chain length, only valid if cacheLength was specified in the
   * constructor.
   */
  size_t chainLength() const {
    if (FOLLY_UNLIKELY(!options_.cacheChainLength)) {
      throw std::invalid_argument("IOBufQueue: chain length not cached");
    }
    dcheckCacheIntegrity();
    return chainLength_ + (cachePtr_->cachedRange.first - tailStart_);
  }

  /**
   * Returns true iff the IOBuf chain length is 0.
   */
  bool empty() const {
    dcheckCacheIntegrity();
    return !head_ ||
        (head_->empty() && cachePtr_->cachedRange.first == tailStart_);
  }

  const Options& options() const { return options_; }

  /**
   * Clear the queue, freeing all the buffers. Options are preserved.
   */
  void reset() { move(); }

  /**
   * Clear the queue, but try to clear and keep the largest buffer for reuse
   * when possible. Options are preserved.
   */
  void clearAndTryReuseLargestBuffer();

  /**
   * Append the queue to a std::string. Non-destructive.
   */
  void appendToString(std::string& out) const;

  /**
   * Calls IOBuf::gather() on the head of the queue, if it exists.
   */
  void gather(std::size_t maxLength);

  /** Movable */
  IOBufQueue(IOBufQueue&&) noexcept;
  IOBufQueue& operator=(IOBufQueue&&);

  static constexpr size_t kMaxPackCopy = 4096;

 private:
  std::unique_ptr<folly::IOBuf> split(size_t n, bool throwOnUnderflow);

  static const size_t kChainLengthNotCached = (size_t)-1;
  /** Not copyable */
  IOBufQueue(const IOBufQueue&) = delete;
  IOBufQueue& operator=(const IOBufQueue&) = delete;

  Options options_;

  // NOTE that chainLength_ is still updated even if !options_.cacheChainLength
  // because doing it unchecked in postallocate() is faster (no (mis)predicted
  // branch)
  mutable size_t chainLength_{0};
  /**
   * Everything that has been appended but not yet discarded or moved out
   * Note: anything that needs to operate on a tail should either call
   * flushCache() or grab updateGuard() (it will flush the cache itself).
   */
  std::unique_ptr<folly::IOBuf> head_;

  mutable uint8_t* tailStart_{nullptr};
  WritableRangeCacheData* cachePtr_{nullptr};
  WritableRangeCacheData localCache_;

  // Non-null only if points to the current tail buffer, and that buffer was
  // originally created by this IOBufQueue, so it can be safely
  // reused. Initially set by preallocateSlow() and updated by maybeReuseTail()
  // or invalidated by updateGuard().
  folly::IOBuf* reusableTail_ = nullptr;

  void dcheckCacheIntegrity() const {
    // Tail start should always be less than tail end.
    DCHECK_LE((void*)tailStart_, (void*)cachePtr_->cachedRange.first);
    DCHECK_LE(
        (void*)cachePtr_->cachedRange.first,
        (void*)cachePtr_->cachedRange.second);
    DCHECK(
        cachePtr_->cachedRange.first != nullptr ||
        cachePtr_->cachedRange.second == nullptr);

    // There is always an attached cache instance.
    DCHECK(cachePtr_->attached);

    // Either cache is empty or it coincides with the tail.
    if (cachePtr_->cachedRange.first != nullptr) {
      DCHECK(head_ != nullptr);
      DCHECK(tailStart_ == head_->prev()->writableTail());
      DCHECK(tailStart_ <= cachePtr_->cachedRange.first);
      DCHECK(cachePtr_->cachedRange.first >= head_->prev()->writableTail());
      DCHECK(
          cachePtr_->cachedRange.second ==
          head_->prev()->writableTail() + head_->prev()->tailroom());
    }

    // If reusableTail_ is not null it should point to the current tail buffer.
    if (reusableTail_ != nullptr) {
      DCHECK(head_ != nullptr);
      DCHECK(reusableTail_ == head_->prev());
    }
  }

  /**
   * Populate dest with writable tail range cache.
   */
  void fillWritableRangeCache(WritableRangeCacheData& dest) {
    dcheckCacheIntegrity();
    if (cachePtr_ != &dest) {
      dest = std::move(*cachePtr_);
      cachePtr_ = &dest;
    }
  }

  /**
   * Clear current writable tail cache and reset it to localCache_
   */
  void clearWritableRangeCache() {
    flushCache();

    if (cachePtr_ != &localCache_) {
      localCache_ = std::move(*cachePtr_);
      cachePtr_ = &localCache_;
    }

    DCHECK(cachePtr_ == &localCache_ && localCache_.attached);
  }

  /**
   * Commit any pending changes to the tail of the queue.
   */
  void flushCache() const {
    dcheckCacheIntegrity();

    if (tailStart_ != cachePtr_->cachedRange.first) {
      auto buf = head_->prev();
      DCHECK_EQ(
          (void*)(buf->writableTail() + buf->tailroom()),
          (void*)cachePtr_->cachedRange.second);
      auto len = cachePtr_->cachedRange.first - tailStart_;
      buf->append(len);
      chainLength_ += len;
      tailStart_ += len;
    }
  }

  // For WritableRangeCache move assignment/construction.
  void updateCacheRef(WritableRangeCacheData& newRef) { cachePtr_ = &newRef; }

  /**
   * Update cached writable tail range. Called by updateGuard()
   */
  void updateWritableTailCache() {
    if (head_ == nullptr || reusableTail_ != head_->prev()) {
      reusableTail_ = nullptr;
    }

    if (FOLLY_LIKELY(head_ != nullptr)) {
      IOBuf* buf = head_->prev();
      if (FOLLY_LIKELY(!buf->isSharedOne())) {
        tailStart_ = buf->writableTail();
        cachePtr_->cachedRange = std::pair<uint8_t*, uint8_t*>(
            tailStart_, tailStart_ + buf->tailroom());
        return;
      }
    }
    tailStart_ = nullptr;
    cachePtr_->cachedRange = std::pair<uint8_t*, uint8_t*>();
  }

  std::pair<void*, std::size_t> preallocateSlow(
      std::size_t min, std::size_t newAllocationSize, std::size_t max);

  void maybeReuseTail();
};

} // namespace folly
