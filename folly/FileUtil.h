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

#ifndef FOLLY_FILEUTIL_H_
#define FOLLY_FILEUTIL_H_

#include <sys/uio.h>
#include <unistd.h>

namespace folly {

/**
 * Convenience wrappers around some commonly used system calls.  The *NoInt
 * wrappers retry on EINTR.  The *Full wrappers retry on EINTR and also loop
 * until all data is written.  Note that *Full wrappers weaken the thread
 * semantics of underlying system calls.
 */
int closeNoInt(int fd);

ssize_t readNoInt(int fd, void* buf, size_t n);
ssize_t preadNoInt(int fd, void* buf, size_t n, off_t offset);
ssize_t readvNoInt(int fd, const struct iovec* iov, int count);

ssize_t writeNoInt(int fd, const void* buf, size_t n);
ssize_t pwriteNoInt(int fd, const void* buf, size_t n, off_t offset);
ssize_t writevNoInt(int fd, const struct iovec* iov, int count);

/**
 * Wrapper around read() (and pread()) that, in addition to retrying on
 * EINTR, will loop until all data is read.
 *
 * This wrapper is only useful for blocking file descriptors (for non-blocking
 * file descriptors, you have to be prepared to deal with incomplete reads
 * anyway), and only exists because POSIX allows read() to return an incomplete
 * read if interrupted by a signal (instead of returning -1 and setting errno
 * to EINTR).
 *
 * Note that this wrapper weakens the thread safety of read(): the file pointer
 * is shared between threads, but the system call is atomic.  If multiple
 * threads are reading from a file at the same time, you don't know where your
 * data came from in the file, but you do know that the returned bytes were
 * contiguous.  You can no longer make this assumption if using readFull().
 * You should probably use pread() when reading from the same file descriptor
 * from multiple threads simultaneously, anyway.
 */
ssize_t readFull(int fd, void* buf, size_t n);
ssize_t preadFull(int fd, void* buf, size_t n, off_t offset);
// TODO(tudorb): add readvFull if needed

/**
 * Similar to readFull and preadFull above, wrappers around write() and
 * pwrite() that loop until all data is written.
 *
 * Generally, the write() / pwrite() system call may always write fewer bytes
 * than requested, just like read().  In certain cases (such as when writing to
 * a pipe), POSIX provides stronger guarantees, but not in the general case.
 * For example, Linux (even on a 64-bit platform) won't write more than 2GB in
 * one write() system call.
 */
ssize_t writeFull(int fd, const void* buf, size_t n);
ssize_t pwriteFull(int fd, const void* buf, size_t n, off_t offset);
// TODO(tudorb): add writevFull if needed

}  // namespaces

#endif /* FOLLY_FILEUTIL_H_ */

