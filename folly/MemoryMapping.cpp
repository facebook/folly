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

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <system_error>
#include <gflags/gflags.h>

DEFINE_int64(mlock_chunk_size, 1 << 20,  // 1MB
             "Maximum bytes to mlock/munlock/munmap at once "
             "(will be rounded up to PAGESIZE)");

namespace folly {

/* protected constructor */
MemoryMapping::MemoryMapping()
  : mapStart_(nullptr)
  , mapLength_(0)
  , locked_(false) {
}

MemoryMapping::MemoryMapping(File file, off_t offset, off_t length)
  : mapStart_(nullptr)
  , mapLength_(0)
  , locked_(false) {

  init(std::move(file), offset, length, PROT_READ, false);
}

MemoryMapping::MemoryMapping(const char* name, off_t offset, off_t length)
  : MemoryMapping(File(name), offset, length) { }

MemoryMapping::MemoryMapping(int fd, off_t offset, off_t length)
  : MemoryMapping(File(fd), offset, length) { }

void MemoryMapping::init(File file,
                         off_t offset, off_t length,
                         int prot,
                         bool grow) {
  off_t pageSize = sysconf(_SC_PAGESIZE);
  CHECK_GE(offset, 0);

  // Round down the start of the mapped region
  size_t skipStart = offset % pageSize;
  offset -= skipStart;

  file_ = std::move(file);
  mapLength_ = length;
  if (mapLength_ != -1) {
    mapLength_ += skipStart;

    // Round up the end of the mapped region
    mapLength_ = (mapLength_ + pageSize - 1) / pageSize * pageSize;
  }

  // stat the file
  struct stat st;
  CHECK_ERR(fstat(file_.fd(), &st));
  off_t remaining = st.st_size - offset;
  if (mapLength_ == -1) {
    length = mapLength_ = remaining;
  } else {
    if (length > remaining) {
      if (grow) {
        PCHECK(0 == ftruncate(file_.fd(), offset + length))
          << "ftructate() failed, couldn't grow file";
        remaining = length;
      } else {
        length = remaining;
      }
    }
    if (mapLength_ > remaining) mapLength_ = remaining;
  }

  if (length == 0) {
    mapLength_ = 0;
    mapStart_ = nullptr;
  } else {
    unsigned char* start = static_cast<unsigned char*>(
      mmap(nullptr, mapLength_, prot, MAP_SHARED, file_.fd(), offset));
    PCHECK(start != MAP_FAILED)
      << " offset=" << offset
      << " length=" << mapLength_;
    mapStart_ = start;
    data_.reset(start + skipStart, length);
  }
}

namespace {

off_t memOpChunkSize(off_t length) {
  off_t chunkSize = length;
  if (FLAGS_mlock_chunk_size <= 0) {
    return chunkSize;
  }

  chunkSize = FLAGS_mlock_chunk_size;
  off_t pageSize = sysconf(_SC_PAGESIZE);
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
                   void* mem, size_t bufSize,
                   size_t& amountSucceeded) {
  // unmap/mlock/munlock take a kernel semaphore and block other threads from
  // doing other memory operations. If the size of the buffer is big the
  // semaphore can be down for seconds (for benchmarks see
  // http://kostja-osipov.livejournal.com/42963.html).  Doing the operations in
  // chunks breaks the locking into intervals and lets other threads do memory
  // operations of their own.

  size_t chunkSize = memOpChunkSize(bufSize);

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
  locked_ = memOpInChunks(::mlock, mapStart_, mapLength_, amountSucceeded);
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
  if (!memOpInChunks(::munlock, mapStart_, amountSucceeded, amountSucceeded)) {
    PLOG(WARNING) << "munlock()";
  }

  return false;
}

void MemoryMapping::munlock(bool dontneed) {
  if (!locked_) return;

  size_t amountSucceeded = 0;
  if (!memOpInChunks(::munlock, mapStart_, mapLength_, amountSucceeded)) {
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
    if (!memOpInChunks(::munmap, mapStart_, mapLength_, amountSucceeded)) {
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

WritableMemoryMapping::WritableMemoryMapping(File file, off_t offset, off_t length) {
  init(std::move(file), offset, length, PROT_READ | PROT_WRITE, true);
}

}  // namespace folly
