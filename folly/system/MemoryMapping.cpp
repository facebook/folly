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

#include <folly/system/MemoryMapping.h>

#include <fcntl.h>
#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <system_error>
#include <utility>

#include <fmt/core.h>
#include <glog/logging.h>

#include <folly/Portability.h>
#include <folly/experimental/io/HugePages.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/SysSyscall.h>
#include <folly/portability/Unistd.h>

static constexpr ssize_t kDefaultMlockChunkSize = !folly::kMscVer
    // Linux implementations of unmap/mlock/munlock take a kernel
    // semaphore and block other threads from doing other memory
    // operations. Split the operations in chunks.
    ? (1 << 20) // 1MB
    // MSVC doesn't have this problem, and calling munmap many times
    // with the same address is a bad idea with the windows implementation.
    : (-1);

FOLLY_GFLAGS_DEFINE_int64(
    mlock_chunk_size,
    kDefaultMlockChunkSize,
    "Maximum bytes to mlock/munlock/munmap at once "
    "(will be rounded up to PAGESIZE). Ignored if negative.");

namespace folly {

namespace {

enum mmap_flags : int {
#ifdef MAP_POPULATE
  populate = MAP_POPULATE,
#else
  populate = 0,
#endif
};

} // namespace

MemoryMapping::MemoryMapping(MemoryMapping&& other) noexcept {
  swap(other);
}

MemoryMapping::MemoryMapping(
    File file, off64_t offset, off64_t length, Options options)
    : file_(std::move(file)), options_(options) {
  CHECK(file_);
  init(offset, length);
}

MemoryMapping::MemoryMapping(
    const char* name, off64_t offset, off64_t length, Options options)
    : MemoryMapping(
          File(name, options.writable ? O_RDWR : O_RDONLY),
          offset,
          length,
          options) {}

MemoryMapping::MemoryMapping(
    int fd, off64_t offset, off64_t length, Options options)
    : MemoryMapping(File(fd), offset, length, options) {}

MemoryMapping::MemoryMapping(AnonymousType, off64_t length, Options options)
    : options_(options) {
  init(0, length);
}

namespace {

void getDeviceOptions(dev_t device, off64_t& pageSize, bool& autoExtend) {
  if constexpr (kIsLinux) {
    auto ps = getHugePageSizeForDevice(device);
    if (ps) {
      pageSize = ps->size;
      autoExtend = true;
    }
  }
}

} // namespace

void MemoryMapping::init(off64_t offset, off64_t length) {
  const bool grow = options_.grow;
  const bool anon = !file_;
  CHECK(!(grow && anon));

  off64_t& pageSize = options_.pageSize;

#ifdef _WIN32
  // stat that support files larger then 4Gb
  struct _stat64 st;
#else
  struct stat st;
#endif

  // On Linux, hugetlbfs file systems don't require ftruncate() to grow the
  // file, and (on kernels before 2.6.24) don't even allow it. Also, the file
  // size is always a multiple of the page size.
  bool autoExtend = false;

  if (!anon) {
// Stat the file
#ifdef _WIN32
    CHECK_ERR(_fstat64(file_.fd(), &st));
#else
    CHECK_ERR(fstat(file_.fd(), &st));
#endif
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
    pageSize = off64_t(sysconf(_SC_PAGESIZE));
  }

  CHECK_GT(pageSize, 0);
  CHECK_EQ(pageSize & (pageSize - 1), 0); // power of two
  CHECK_GE(offset, 0);

  // Round down the start of the mapped region
  off64_t skipStart = offset % pageSize;
  offset -= skipStart;

  mapLength_ = length;
  if (mapLength_ != -1) {
    mapLength_ += skipStart;

    // Round up the end of the mapped region
    mapLength_ = (mapLength_ + pageSize - 1) / pageSize * pageSize;
  }

  off64_t remaining = anon ? length : st.st_size - offset;

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
    if (anon) {
      flags |= MAP_ANONYMOUS;
    }
    if (options_.prefault) {
      flags |= mmap_flags::populate;
    }

    // The standard doesn't actually require PROT_NONE to be zero...
    int prot = PROT_NONE;
    if (options_.readable || options_.writable) {
      prot =
          ((options_.readable ? PROT_READ : 0) |
           (options_.writable ? PROT_WRITE : 0));
    }

#ifdef _WIN32
    auto start = static_cast<unsigned char*>(mmap64(
        options_.address, size_t(mapLength_), prot, flags, file_.fd(), offset));
#else
    auto start = static_cast<unsigned char*>(mmap(
        options_.address, size_t(mapLength_), prot, flags, file_.fd(), offset));
#endif
    PCHECK(start != MAP_FAILED)
        << " offset=" << offset << " length=" << mapLength_;
    mapStart_ = start;
    data_.reset(start + skipStart, size_t(length));
  }
}

namespace {

off64_t memOpChunkSize(off64_t length, off64_t pageSize) {
  off64_t chunkSize = length;
  if (FLAGS_mlock_chunk_size <= 0) {
    return chunkSize;
  }

  chunkSize = off64_t(FLAGS_mlock_chunk_size);
  off64_t r = chunkSize % pageSize;
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
template <typename Op>
bool memOpInChunks(
    Op op,
    void* mem,
    size_t bufSize,
    off64_t pageSize,
    size_t& amountSucceeded) {
  // Linux' unmap/mlock/munlock take a kernel semaphore and block other threads
  // from doing other memory operations. If the size of the buffer is big the
  // semaphore can be down for seconds (for benchmarks see
  // http://kostja-osipov.livejournal.com/42963.html).  Doing the operations in
  // chunks breaks the locking into intervals and lets other threads do memory
  // operations of their own.

  auto chunkSize = size_t(memOpChunkSize(off64_t(bufSize), pageSize));

  auto addr = static_cast<char*>(mem);
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

} // namespace

int mlock2wrapper(
    [[maybe_unused]] const void* addr,
    [[maybe_unused]] size_t len,
    MemoryMapping::LockFlags flags) {
  int intFlags = 0;
  (void)intFlags;
  if (flags.lockOnFault) {
    // MLOCK_ONFAULT, only available in non-portable headers.
    intFlags |= 0x01;
  }

#if defined(__GLIBC__) && !defined(__APPLE__)
#if __GLIBC_PREREQ(2, 27)
  return mlock2(addr, len, intFlags);
#elif defined(SYS_mlock2)
  // SYS_mlock2 is defined in Linux headers since 4.4
  return syscall(SYS_mlock2, addr, len, intFlags);
#else // !__GLIBC_PREREQ(2, 27) && !defined(SYS_mlock2)
  errno = ENOSYS;
  return -1;
#endif
#else // !defined(__GLIBC__) || defined(__APPLE__)
  errno = ENOSYS;
  return -1;
#endif
}

bool MemoryMapping::mlock(LockMode mode, LockFlags flags) {
  size_t amountSucceeded = 0;
  locked_ = memOpInChunks(
      [flags](void* addr, size_t len) -> int {
        // If no flags are set, mlock2() behaves exactly the same as
        // mlock(). Prefer the portable variant.
        return flags == LockFlags{}
            ? ::mlock(addr, len)
            : mlock2wrapper(addr, len, flags);
      },
      mapStart_,
      size_t(mapLength_),
      options_.pageSize,
      amountSucceeded);
  if (locked_) {
    return true;
  }

  auto msg = fmt::format("mlock({}) failed at {}", mapLength_, amountSucceeded);
  if (mode == LockMode::TRY_LOCK && errno == EPERM) {
    PLOG(WARNING) << msg;
  } else if (mode == LockMode::TRY_LOCK && errno == ENOMEM) {
    VLOG(1) << msg;
  } else {
    PLOG(FATAL) << msg;
  }

  // only part of the buffer was mlocked, unlock it back
  if (!memOpInChunks(
          ::munlock,
          mapStart_,
          amountSucceeded,
          options_.pageSize,
          amountSucceeded)) {
    PLOG(WARNING) << "munlock()";
  }

  return false;
}

void MemoryMapping::munlock(bool dontneed) {
  if (!locked_) {
    return;
  }

  size_t amountSucceeded = 0;
  if (!memOpInChunks(
          ::munlock,
          mapStart_,
          size_t(mapLength_),
          options_.pageSize,
          amountSucceeded)) {
    PLOG(WARNING) << "munlock()";
  }
  if (mapLength_ && dontneed &&
      ::madvise(mapStart_, size_t(mapLength_), MADV_DONTNEED)) {
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
    if (!memOpInChunks(
            ::munmap,
            mapStart_,
            size_t(mapLength_),
            options_.pageSize,
            amountSucceeded)) {
      PLOG(FATAL) << fmt::format(
          "munmap({}) failed at {}", mapLength_, amountSucceeded);
    }
  }
}

void MemoryMapping::advise(int advice) const {
  advise(advice, 0, size_t(mapLength_));
}

void MemoryMapping::advise(int advice, size_t offset, size_t length) const {
  CHECK_LE(offset + length, size_t(mapLength_))
      << " offset: " << offset << " length: " << length
      << " mapLength_: " << mapLength_;

  // Include the entire start page: round down to page boundary.
  const auto offMisalign = offset % options_.pageSize;
  offset -= offMisalign;
  length += offMisalign;

  // Round the last page down to page boundary.
  if (offset + length != size_t(mapLength_)) {
    length -= length % options_.pageSize;
  }

  if (length == 0) {
    return;
  }

  char* mapStart = static_cast<char*>(mapStart_) + offset;
  PLOG_IF(WARNING, ::madvise(mapStart, length, advice)) << "madvise";
}

MemoryMapping& MemoryMapping::operator=(MemoryMapping&& other) {
  swap(other);
  return *this;
}

void MemoryMapping::swap(MemoryMapping& other) noexcept {
  using std::swap;
  swap(this->file_, other.file_);
  swap(this->mapStart_, other.mapStart_);
  swap(this->mapLength_, other.mapLength_);
  swap(this->options_, other.options_);
  swap(this->locked_, other.locked_);
  swap(this->data_, other.data_);
}

void swap(MemoryMapping& a, MemoryMapping& b) noexcept {
  a.swap(b);
}

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
      off64_t(srcMap.range().size()),
      MemoryMapping::writable());

  alignedForwardMemcpy(
      destMap.writableRange().data(),
      srcMap.range().data(),
      srcMap.range().size());
}

bool MemoryMapping::LockFlags::operator==(const LockFlags& other) const {
  return lockOnFault == other.lockOnFault;
}

} // namespace folly
