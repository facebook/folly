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

#ifndef _WIN32

#include <unistd.h>

#if defined(__APPLE__) || defined(__EMSCRIPTEN__)
typedef off_t off64_t;

off64_t lseek64(int fh, off64_t off, int orig);

ssize_t pread64(int fd, void* buf, size_t count, off64_t offset);

#endif

#else

#include <cstdint>

#include <process.h> // @manual

#include <sys/locking.h> // @manual

#include <folly/Portability.h>
#include <folly/portability/SysTypes.h>

// This is different from the normal headers because there are a few cases,
// such as close(), where we need to override the definition of an existing
// function. To avoid conflicts at link time, everything here is in a namespace
// which is then used globally.

#define _SC_PAGESIZE 1
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN 2
#define _SC_NPROCESSORS_CONF 2
#define _SC_LEVEL1_DCACHE_LINESIZE 3

// Windows doesn't define these, but these are the correct values
// for Windows.
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Windows is weird and doesn't actually defined these
// for the parameters to access, so we have to do it ourselves -_-...
#define F_OK 0
#define X_OK F_OK
#define W_OK 2
#define R_OK 4
#define RW_OK 6

#define F_LOCK _LK_LOCK
#define F_ULOCK _LK_UNLCK

namespace folly {
namespace portability {
namespace unistd {
using off64_t = int64_t;
int fsync(int fd);
int ftruncate(int fd, off_t len);
int getdtablesize();
int getgid();
pid_t getppid();
int getuid();
int lockf(int fd, int cmd, off_t len);
off64_t lseek64(int fh, off64_t off, int orig);
ssize_t pread(int fd, void* buf, size_t count, off_t offset);
ssize_t pread64(int fd, void* buf, size_t count, off64_t offset);
ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
ssize_t readlink(const char* path, char* buf, size_t buflen);
void* sbrk(intptr_t i);
unsigned int sleep(unsigned int seconds);
long sysconf(int tp);
int truncate(const char* path, off_t len);
int usleep(unsigned int ms);
} // namespace unistd
} // namespace portability
} // namespace folly

FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wheader-hygiene")
/* using override */ using namespace folly::portability::unistd;
FOLLY_POP_WARNING
#endif

namespace folly {
namespace fileops {
#ifdef _WIN32
int close(int fh);
ssize_t read(int fh, void* buf, size_t mcc);

/// Create a pipe, returning the file descriptors in `pth`.
///
/// On windows this has different behavior than the traditional posix pipe.
/// The returned file descriptors are unix sockets for compatibility with
/// libevent. Also, they allow bidirectional reads and writes,
/// unlike posix which only supports a single direction.
/// @file
int pipe(int pth[2]);
ssize_t write(int fh, void const* buf, size_t count);
#else
using ::close;
using ::pipe;
using ::read;
using ::write;
#endif
} // namespace fileops
} // namespace folly
