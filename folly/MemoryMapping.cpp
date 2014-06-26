/*
 * Copyright 2014 Facebook, Inc.
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

#include "folly/MemoryMapping.h"
#include "folly/Format.h"
#include "folly/Portability.h"

#ifdef __linux__
#include "folly/experimental/io/HugePages.h"
#endif

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <system_error>
#include <gflags/gflags.h>

DEFINE_int64(mlock_chunk_size, 1 << 20,  // 1MB
             "Maximum bytes to mlock/munlock/munmap at once "
             "(will be rounded up to PAGESIZE)");

#ifndef MAP_POPULATE
#define MAP_POPULATE 0
#endif

namespace folly {

MemoryMapping::MemoryMapping(MemoryMapping&& other) {
  swap(other);
}

MemoryMapping::MemoryMapping(File file, off_t offset, off_t length,
                             Options options)
  : file_(std::move(file)),
    options_(std::move(options)) {
  CHECK(file_);
  init(offset, length);
}

MemoryMapping::MemoryMapping(const char* name, off_t offset, off_t length,
                             Options options)
  : MemoryMapping(File(name), offset, length, options) { }

MemoryMapping::MemoryMapping(int fd, off_t offset, off_t length,
                             Options options)
  : MemoryMapping(File(fd), offset, length, options) { }

MemoryMapping::MemoryMapping(AnonymousType, off_t length, Options options)
  : options_(std::move(options)) {
  init(0, length);
}

namespace {

#ifdef __linux__
void getDeviceOptions(dev_t device, off_t& pageSize, bool& autoExtend) {
  auto ps = getHugePageSizeForDevice(device);
  if (ps) {
    pageSize = ps->size;
    autoExtend = true;
  }
}
#else
inline void getDeviceOptions(dev_t device, off_t& pageSize,
                             bool& autoExtend) { }
#endif

}  // namespace

void MemoryMapping::init(off_t offset, off_t length) {
  const bool grow = options_.grow;
  const bool anon = !file_;
  CHECK(!(grow && anon));

  off_t& pageSize = options_.pageSize;

  struct stat st;

  // On Linux, hugetlbfs file systems don't require ftruncate() to grow the
  // file, and (on kernels before 2.6.24) don't even allow it. Also, the file
  // size is always a multiple of the page size.
  bool autoExtend = false;

  if (!anon) {
    // Stat the file
    CHECK_ERR(fstat(file_.fd(), &st));

    if (pageSize == 0) {
      getDeviceOptions(st.st_dev, pageSize, autoExtend);
    }
  } else {
    DCHECK(!file_);
    DCHECK_EQ(offset, 0);
    CHECK_EQ(pageSize, 0);
    CHECK_GE(length, 0);
  }

  if (pageSize == 0) {
    pageSize = sysconf(_SC_PAGESIZE);
  }

  CHECK_GT(pageSize, 0);
  CHECK_EQ(pageSize & (pageSize - 1), 0);  // power of two
  CHECK_GE(offset, 0);

  // Round down the start of the mapped region
  size_t skipStart = offset % pageSize;
  offset -= skipStart;

  mapLength_ = length;
  if (mapLength_ != -1) {
    mapLength_ += skipStart;

    // Round up the end of the mapped region
    mapLength_ = (mapLength_ + pageSize - 1) / pageSize * pageSize;
  }

  off_t remaining = anon ? length : st.st_size - offset;

  if (mapLength_ == -1) {
    length = mapLength_ = remaining;
  } else {
    if (length > remaining) {
      if (grow) {
        if (!autoExtend) {
          PCHECK(0 == ftruncate(file_.fd(), offset + length))
            << "ftruncate() failed, couldn't grow file to "
            << offset + length;
          remaining = length;
        } else {
          // Extend mapping to multiple of page size, don't use ftruncate
          remaining = mapLength_;
        }
      } else {
        length = remaining;
      }
    }
    if (mapLength_ > remaining) {
      mapLength_ = remaining;
    }
  }

  if (length == 0) {
    mapLength_ = 0;
    mapStart_ = nullptr;
  } else {
    int flags = options_.shared ? MAP_SHARED : MAP_PRIVATE;
    if (anon) flags |= MAP_ANONYMOUS;
    if (options_.prefault) flags |= MAP_POPULATE;

    // The standard doesn't actually require PROT_NONE to be zero...
    int prot = PROT_NONE;
    if (options_.readable || options_.writable) {
      prot = ((options_.readable ? PROT_READ : 0) |
              (options_.writable ? PROT_WRITE : 0));
    }

    unsigned char* start = static_cast<unsigned char*>(
      mmap(options_.address, mapLength_, prot, flags, file_.fd(), offset));
    PCHECK(start != MAP_FAILED)
      << " offset=" << offset
      << " length=" << mapLength_;
    mapStart_ = start;
    data_.reset(start + skipStart, length);
  }
}

namespace {

off_t memOpChunkSize(off_t length, off_t pageSize) {
  off_t chunkSize = length;
  if (FLAGS_mlock_chunk_size <= 0) {
    return chunkSize;
  }

  chunkSize = FLAGS_mlock_chunk_size;
  off_t r = chunkSize % pageSize;
  if (r) {
    chunkSize += (pageSize - r);
  }
  return chunkSize;
}

/**
 * Run @op in chunks over the buffer @mem of @bufSize length.
 *
 * Return:
 * - success: true + amountSucceeded == bufSize (op success on whole buffer)
 * - failure: false + amountSucceeded == nr bytes on which op succeeded.
 */
bool memOpInChunks(std::function<int(void*, size_t)> op,
                   void* mem, size_t bufSize, off_t pageSize,
                   size_t& amountSucceeded) {
  // unmap/mlock/munlock take a kernel semaphore and block other threads from
  // doing other memory operations. If the size of the buffer is big the
  // semaphore can be down for seconds (for benchmarks see
  // http://kostja-osipov.livejournal.com/42963.html).  Doing the operations in
  // chunks breaks the locking into intervals and lets other threads do memory
  // operations of their own.

  size_t chunkSize = memOpChunkSize(bufSize, pageSize);

  char* addr = static_cast<char*>(mem);
  amountSucceeded = 0;

  while (amountSucceeded < bufSize) {
    size_t size = std::min(chunkSize, bufSize - amountSucceeded);
    if (op(addr + amountSucceeded, size) != 0) {
      return false;
    }
    amountSucceeded += size;
  }

  return true;
}

}  // anonymous namespace

bool MemoryMapping::mlock(LockMode lock) {
  size_t amountSucceeded = 0;
  locked_ = memOpInChunks(::mlock, mapStart_, mapLength_, options_.pageSize,
                          amountSucceeded);
  if (locked_) {
    return true;
  }

  auto msg(folly::format(
    "mlock({}) failed at {}",
    mapLength_, amountSucceeded).str());

  if (lock == LockMode::TRY_LOCK && (errno == EPERM || errno == ENOMEM)) {
    PLOG(WARNING) << msg;
  } else {
    PLOG(FATAL) << msg;
  }

  // only part of the buffer was mlocked, unlock it back
  if (!memOpInChunks(::munlock, mapStart_, amountSucceeded, options_.pageSize,
                     amountSucceeded)) {
    PLOG(WARNING) << "munlock()";
  }

  return false;
}

void MemoryMapping::munlock(bool dontneed) {
  if (!locked_) return;

  size_t amountSucceeded = 0;
  if (!memOpInChunks(::munlock, mapStart_, mapLength_, options_.pageSize,
                     amountSucceeded)) {
    PLOG(WARNING) << "munlock()";
  }
  if (mapLength_ && dontneed &&
      ::madvise(mapStart_, mapLength_, MADV_DONTNEED)) {
    PLOG(WARNING) << "madvise()";
  }
  locked_ = false;
}

void MemoryMapping::hintLinearScan() {
  advise(MADV_SEQUENTIAL);
}

MemoryMapping::~MemoryMapping() {
  if (mapLength_) {
    size_t amountSucceeded = 0;
    if (!memOpInChunks(::munmap, mapStart_, mapLength_, options_.pageSize,
                       amountSucceeded)) {
      PLOG(FATAL) << folly::format(
        "munmap({}) failed at {}",
        mapLength_, amountSucceeded).str();
    }
  }
}

void MemoryMapping::advise(int advice) const {
  if (mapLength_ && ::madvise(mapStart_, mapLength_, advice)) {
    PLOG(WARNING) << "madvise()";
  }
}

MemoryMapping& MemoryMapping::operator=(MemoryMapping other) {
  swap(other);
  return *this;
}

void MemoryMapping::swap(MemoryMapping& other) {
  using std::swap;
  swap(this->file_, other.file_);
  swap(this->mapStart_, other.mapStart_);
  swap(this->mapLength_, other.mapLength_);
  swap(this->options_, other.options_);
  swap(this->locked_, other.locked_);
  swap(this->data_, other.data_);
}

void swap(MemoryMapping& a, MemoryMapping& b) { a.swap(b); }

void alignedForwardMemcpy(void* dst, const void* src, size_t size) {
  assert(reinterpret_cast<uintptr_t>(src) % alignof(unsigned long) == 0);
  assert(reinterpret_cast<uintptr_t>(dst) % alignof(unsigned long) == 0);

  auto srcl = static_cast<const unsigned long*>(src);
  auto dstl = static_cast<unsigned long*>(dst);

  while (size >= sizeof(unsigned long)) {
    *dstl++ = *srcl++;
    size -= sizeof(unsigned long);
  }

  auto srcc = reinterpret_cast<const unsigned char*>(srcl);
  auto dstc = reinterpret_cast<unsigned char*>(dstl);

  while (size != 0) {
    *dstc++ = *srcc++;
    --size;
  }
}

void mmapFileCopy(const char* src, const char* dest, mode_t mode) {
  MemoryMapping srcMap(src);
  srcMap.hintLinearScan();

  MemoryMapping destMap(
      File(dest, O_RDWR | O_CREAT | O_TRUNC, mode),
      0,
      srcMap.range().size(),
      MemoryMapping::writable());

  alignedForwardMemcpy(destMap.writableRange().data(),
                       srcMap.range().data(),
                       srcMap.range().size());
}

}  // namespace folly
