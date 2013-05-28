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

#include "folly/FileUtil.h"

#include <cerrno>
#ifdef __APPLE__
#include <fcntl.h>
#endif

#include "folly/detail/FileUtilDetail.h"

namespace folly {

using namespace fileutil_detail;

int openNoInt(const char* name, int flags, mode_t mode) {
  return wrapNoInt(open, name, flags, mode);
}

int closeNoInt(int fd) {
  int r = close(fd);
  // Ignore EINTR.  On Linux, close() may only return EINTR after the file
  // descriptor has been closed, so you must not retry close() on EINTR --
  // in the best case, you'll get EBADF, and in the worst case, you'll end up
  // closing a different file (one opened from another thread).
  //
  // Interestingly enough, the Single Unix Specification says that the state
  // of the file descriptor is unspecified if close returns EINTR.  In that
  // case, the safe thing to do is also not to retry close() -- leaking a file
  // descriptor is probably better than closing the wrong file.
  if (r == -1 && errno == EINTR) {
    r = 0;
  }
  return r;
}

int fsyncNoInt(int fd) {
  return wrapNoInt(fsync, fd);
}

int fdatasyncNoInt(int fd) {
#ifndef __APPLE__
  return wrapNoInt(fdatasync, fd);
#else
  return wrapNoInt(fcntl, fd, F_FULLFSYNC);
#endif
}

int ftruncateNoInt(int fd, off_t len) {
  return wrapNoInt(ftruncate, fd, len);
}

int truncateNoInt(const char* path, off_t len) {
  return wrapNoInt(truncate, path, len);
}

ssize_t readNoInt(int fd, void* buf, size_t count) {
  return wrapNoInt(read, fd, buf, count);
}

ssize_t preadNoInt(int fd, void* buf, size_t count, off_t offset) {
  return wrapNoInt(pread, fd, buf, count, offset);
}

ssize_t readvNoInt(int fd, const iovec* iov, int count) {
  return wrapNoInt(writev, fd, iov, count);
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

ssize_t readvFull(int fd, iovec* iov, int count) {
  return wrapvFull(readv, fd, iov, count);
}

#ifdef FOLLY_HAVE_PREADV
ssize_t preadvFull(int fd, iovec* iov, int count, off_t offset) {
  return wrapvFull(preadv, fd, iov, count, offset);
}
#endif

ssize_t writevFull(int fd, iovec* iov, int count) {
  return wrapvFull(writev, fd, iov, count);
}

#ifdef FOLLY_HAVE_PWRITEV
ssize_t pwritevFull(int fd, iovec* iov, int count, off_t offset) {
  return wrapvFull(pwritev, fd, iov, count, offset);
}
#endif

}  // namespaces

