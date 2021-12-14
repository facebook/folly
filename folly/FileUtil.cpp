/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/FileUtil.h>

#include <cerrno>
#include <string>
#include <system_error>
#include <vector>

#include <folly/detail/FileUtilDetail.h>
#include <folly/detail/FileUtilVectorDetail.h>
#include <folly/net/NetOps.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Stdlib.h>
#include <folly/portability/SysFile.h>
#include <folly/portability/SysStat.h>

namespace folly {

using namespace fileutil_detail;

int openNoInt(const char* name, int flags, mode_t mode) {
  // Android NDK bionic with FORTIFY has this definition:
  // https://android.googlesource.com/platform/bionic/+/9349b9e51b/libc/include/bits/fortify/fcntl.h
  // ```
  // __BIONIC_ERROR_FUNCTION_VISIBILITY
  // int open(const char* pathname, int flags, mode_t modes, ...) __overloadable
  //         __errorattr(__open_too_many_args_error);
  // ```
  // This is originally to prevent open() with incorrect parameters.
  //
  // However, combined with folly wrapNotInt, template deduction will fail.
  // In this case, we create a custom lambda to bypass the error.
  // The solution is referenced from
  // https://github.com/llvm/llvm-project/commit/0a0e411204a2baa520fd73a8d69b664f98b428ba
  //
  auto openWrapper = [&] { return open(name, flags, mode); };
  return int(wrapNoInt(openWrapper));
}

static int filterCloseReturn(int r) {
  // Ignore EINTR.  On Linux, close() may only return EINTR after the file
  // descriptor has been closed, so you must not retry close() on EINTR --
  // in the best case, you'll get EBADF, and in the worst case, you'll end up
  // closing a different file (one opened from another thread).
  //
  // Interestingly enough, the Single Unix Specification says that the state
  // of the file descriptor is unspecified if close returns EINTR.  In that
  // case, the safe thing to do is also not to retry close() -- leaking a file
  // descriptor is definitely better than closing the wrong file.
  if (r == -1 && errno == EINTR) {
    return 0;
  }
  return r;
}

int closeNoInt(int fd) {
  return filterCloseReturn(close(fd));
}

int closeNoInt(NetworkSocket fd) {
  return filterCloseReturn(netops::close(fd));
}

int fsyncNoInt(int fd) {
  return int(wrapNoInt(fsync, fd));
}

int dupNoInt(int fd) {
  return int(wrapNoInt(dup, fd));
}

int dup2NoInt(int oldFd, int newFd) {
  return int(wrapNoInt(dup2, oldFd, newFd));
}

int fdatasyncNoInt(int fd) {
#if defined(__APPLE__)
  return int(wrapNoInt(fcntl, fd, F_FULLFSYNC));
#elif defined(__FreeBSD__) || defined(_MSC_VER)
  return int(wrapNoInt(fsync, fd));
#else
  return int(wrapNoInt(fdatasync, fd));
#endif
}

int ftruncateNoInt(int fd, off_t len) {
  return int(wrapNoInt(ftruncate, fd, len));
}

int truncateNoInt(const char* path, off_t len) {
  return int(wrapNoInt(truncate, path, len));
}

int flockNoInt(int fd, int operation) {
  return int(wrapNoInt(flock, fd, operation));
}

int shutdownNoInt(NetworkSocket fd, int how) {
  return int(wrapNoInt(netops::shutdown, fd, how));
}

ssize_t readNoInt(int fd, void* buf, size_t count) {
  return wrapNoInt(read, fd, buf, count);
}

ssize_t preadNoInt(int fd, void* buf, size_t count, off_t offset) {
  return wrapNoInt(pread, fd, buf, count, offset);
}

ssize_t readvNoInt(int fd, const iovec* iov, int count) {
  return wrapNoInt(readv, fd, iov, count);
}

ssize_t preadvNoInt(int fd, const iovec* iov, int count, off_t offset) {
  return wrapNoInt(preadv, fd, iov, count, offset);
}

ssize_t writeNoInt(int fd, const void* buf, size_t count) {
  return wrapNoInt(write, fd, buf, count);
}

ssize_t pwriteNoInt(int fd, const void* buf, size_t count, off_t offset) {
  return wrapNoInt(pwrite, fd, buf, count, offset);
}

ssize_t writevNoInt(int fd, const iovec* iov, int count) {
  return wrapNoInt(writev, fd, iov, count);
}

ssize_t pwritevNoInt(int fd, const iovec* iov, int count, off_t offset) {
  return wrapNoInt(pwritev, fd, iov, count, offset);
}

ssize_t readFull(int fd, void* buf, size_t count) {
  return wrapFull(read, fd, buf, count);
}

ssize_t preadFull(int fd, void* buf, size_t count, off_t offset) {
  return wrapFull(pread, fd, buf, count, offset);
}

ssize_t writeFull(int fd, const void* buf, size_t count) {
  return wrapFull(write, fd, const_cast<void*>(buf), count);
}

ssize_t pwriteFull(int fd, const void* buf, size_t count, off_t offset) {
  return wrapFull(pwrite, fd, const_cast<void*>(buf), count, offset);
}

#ifndef _WIN32
ssize_t readvFull(int fd, iovec* iov, int count) {
  return wrapvFull(readv, fd, iov, count);
}

ssize_t preadvFull(int fd, iovec* iov, int count, off_t offset) {
  return wrapvFull(preadv, fd, iov, count, offset);
}

ssize_t writevFull(int fd, iovec* iov, int count) {
  return wrapvFull(writev, fd, iov, count);
}

ssize_t pwritevFull(int fd, iovec* iov, int count, off_t offset) {
  return wrapvFull(pwritev, fd, iov, count, offset);
}
#else // _WIN32

// On Windows, the *vFull() functions wrap the simple read/pread/write/pwrite
// functions.  While folly/portability/SysUio.cpp does define readv() and
// writev() implementations for Windows, these attempt to lock the file to
// provide atomicity.  The *vFull() functions do not provide any atomicity
// guarantees, so we can avoid the locking logic.

ssize_t readvFull(int fd, iovec* iov, int count) {
  return wrapvFull(read, fd, iov, count);
}

ssize_t preadvFull(int fd, iovec* iov, int count, off_t offset) {
  return wrapvFull(pread, fd, iov, count, offset);
}

ssize_t writevFull(int fd, iovec* iov, int count) {
  return wrapvFull(write, fd, iov, count);
}

ssize_t pwritevFull(int fd, iovec* iov, int count, off_t offset) {
  return wrapvFull(pwrite, fd, iov, count, offset);
}
#endif // _WIN32

int writeFileAtomicNoThrow(
    StringPiece filename,
    iovec* iov,
    int count,
    mode_t permissions,
    SyncType syncType) {
  // We write the data to a temporary file name first, then atomically rename
  // it into place.  This ensures that the file contents will always be valid,
  // even if we crash or are killed partway through writing out data.
  //
  // Create a buffer that will contain two things:
  // - A nul-terminated version of the filename
  // - The temporary file name
  std::vector<char> pathBuffer;
  // Note that we have to explicitly pass in the size here to make
  // sure the nul byte gets included in the data.
  constexpr folly::StringPiece suffix(".XXXXXX\0", 8);
  pathBuffer.resize((2 * filename.size()) + 1 + suffix.size());
  // Copy in the filename and then a nul terminator
  memcpy(pathBuffer.data(), filename.data(), filename.size());
  pathBuffer[filename.size()] = '\0';
  const char* const filenameCStr = pathBuffer.data();
  // Now prepare the temporary path template
  char* const tempPath = pathBuffer.data() + filename.size() + 1;
  memcpy(tempPath, filename.data(), filename.size());
  memcpy(tempPath + filename.size(), suffix.data(), suffix.size());

  auto tmpFD = mkstemp(tempPath);
  if (tmpFD == -1) {
    return errno;
  }
  bool success = false;
  SCOPE_EXIT {
    if (tmpFD != -1) {
      close(tmpFD);
    }
    if (!success) {
      unlink(tempPath);
    }
  };

  auto rc = writevFull(tmpFD, iov, count);
  if (rc == -1) {
    return errno;
  }

  rc = fchmod(tmpFD, permissions);
  if (rc == -1) {
    return errno;
  }

  // To guarantee atomicity across power failues on POSIX file systems,
  // the temporary file must be explicitly sync'ed before the rename.
  if (syncType == SyncType::WITH_SYNC) {
    rc = fsyncNoInt(tmpFD);
    if (rc == -1) {
      return errno;
    }
  }

  // Close the file before renaming to make sure all data has
  // been successfully written.
  rc = close(tmpFD);
  tmpFD = -1;
  if (rc == -1) {
    return errno;
  }

  rc = rename(tempPath, filenameCStr);
  if (rc == -1) {
    return errno;
  }
  success = true;
  return 0;
}

void writeFileAtomic(
    StringPiece filename,
    iovec* iov,
    int count,
    mode_t permissions,
    SyncType syncType) {
  auto rc = writeFileAtomicNoThrow(filename, iov, count, permissions, syncType);
  if (rc != 0) {
    auto msg = std::string(__func__) + "() failed to update " + filename.str();
    throw std::system_error(rc, std::generic_category(), msg);
  }
}

void writeFileAtomic(
    StringPiece filename,
    ByteRange data,
    mode_t permissions,
    SyncType syncType) {
  iovec iov;
  iov.iov_base = const_cast<unsigned char*>(data.data());
  iov.iov_len = data.size();
  writeFileAtomic(filename, &iov, 1, permissions, syncType);
}

void writeFileAtomic(
    StringPiece filename,
    StringPiece data,
    mode_t permissions,
    SyncType syncType) {
  writeFileAtomic(filename, ByteRange(data), permissions, syncType);
}

} // namespace folly
