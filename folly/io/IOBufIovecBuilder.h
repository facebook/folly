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

#include <deque>
#include <folly/io/IOBuf.h>
#include <folly/memory/Malloc.h>

namespace folly {
/**
 * IOBufIovecBuilder exists to help allocate and fill IOBuf chains using
 * iovec-based scatter/gather APIs.
 *
 * It provides APIs to allocate buffers for use with iovec-based system calls,
 * and then construct IOBuf chains pointing to the buffer data that has been
 * populated.
 *
 * This class allows performing repeated vectored reads and minimizing the
 * number of separate allocations if data is received in small chunks, while
 * still generating unshared IOBuf instances.  For instance, this helps support
 * in place decryption mechanisms which require unshared IOBuf instances.
 */

class IOBufIovecBuilder {
 private:
  /**
   * This is a helper class that is passed to IOBuf::takeOwnership()
   * for use as the custom free function.
   *
   * This class allows multiple IOBuf objects to each point to non-overlapping
   * sections of the same buffer, allowing each IOBuf to consider its buffer
   * as non-shared even though they do share a single allocation.  This class
   * performs additional reference counting to ensure that the entire allocation
   * is freed only when all IOBufs referring to it have been destroyed.
   */
  struct RefCountMem {
    explicit RefCountMem(size_t size) {
      len_ = folly::goodMallocSize(size);
      mem_ = ::malloc(len_);
    }

    ~RefCountMem() { folly::sizedFree(mem_, len_); }

    void* usableMem() const { return static_cast<uint8_t*>(mem_) + used_; }

    size_t usableSize() const { return len_ - used_; }

    void incUsedMem(size_t len) {
      used_ += len;
      DCHECK_LE(used_, len_);
    }

    static void freeMem(void* buf, void* userData) {
      std::ignore = buf;
      reinterpret_cast<RefCountMem*>(userData)->decRef();
    }

    void addRef() { refcount_.fetch_add(1, std::memory_order_acq_rel); }

    void decRef() {
      // Avoid doing atomic decrement if the refcount is 1.
      // This is safe, because it means this is the last reference
      // Anything trying to copy it is already undefined behavior.
      if (refcount_.load(std::memory_order_acquire) > 1) {
        size_t newcnt = refcount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (newcnt > 0) {
          return;
        }
      }

      delete this;
    }

   private:
    std::atomic<size_t> refcount_{1};
    void* mem_{nullptr};
    size_t len_{0};
    size_t used_{0};
  };

 public:
  struct Options {
    static constexpr size_t kDefaultBlockSize = 16 * 1024;
    size_t blockSize_{kDefaultBlockSize};

    Options& setBlockSize(size_t blockSize) {
      blockSize_ = blockSize;

      return *this;
    }
  };

  IOBufIovecBuilder() = delete;
  IOBufIovecBuilder(const IOBufIovecBuilder&) = delete;
  IOBufIovecBuilder(IOBufIovecBuilder&&) = delete;

  IOBufIovecBuilder& operator=(const IOBufIovecBuilder&) = delete;
  IOBufIovecBuilder& operator=(IOBufIovecBuilder&&) = delete;

  explicit IOBufIovecBuilder(const Options& options) : options_(options) {}
  ~IOBufIovecBuilder() {
    for (auto& buf : buffers_) {
      buf->decRef();
    }
  }

  using IoVecVec = std::vector<struct iovec>;
  /**
   * Obtain a vector of writable blocks.  This allocates memory in blocks of
   * `Options::blockSize_` bytes each.  Unused memory from previous calls to
   * `allocateBuffers()` will be re-used, and additional memory will be
   * allocated if necessary.
   *
   * This code will attempt to allocate at least `len` bytes, but may allocate
   * less if that would require more than IOV_MAX chunks.  This API never
   * returns more than IOV_MAX separate blocks.  The returned length allocated
   * may be more than `len` if `len` was not an even multiple of the block size,
   * as the length allocated will be rounded up to the next full block size.
   *
   * The intended use of this API is for the caller to call allocateBuffers()
   * to allocate space, to then fill the buffers using an API such as readv() or
   * recvmsg(), and then obtain an IOBuf chain to the data that was read by
   * calling extractIOBufChain() with the amount of data that was read.
   *
   * @param[out] iovs  vector of `struct iovec` that will be populated
   *                   The vector will be cleared before any new entries
   *                   will be appended
   * @param      len   A requested number of bytes to be available.
   * @return     The number of allocated bytes.
   *
   */
  size_t allocateBuffers(IoVecVec& iovs, size_t len);

  /**
   * Tell the queue that the caller has written data into the first n
   * bytes provided by the previous allocateBuffers() call.
   * @param      len   Number of bytes to be consumed
   * @return     The IOBuf chain
   *
   * @note len should be less than or equal to the size returned by
   *       allocateBuffers().  If len is zero, the caller may skip the call
   *       to postallocate().  If len is nonzero, the caller must not
   *       invoke any other non-const methods on this IOBufIovecBuilder between
   *       the call to allocateBufferse and the call to extractIOBufChain().
   */
  std::unique_ptr<folly::IOBuf> extractIOBufChain(size_t len);

 private:
  Options options_;
  std::deque<RefCountMem*> buffers_;
};
} // namespace folly
