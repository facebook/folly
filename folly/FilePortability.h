/*
* Copyright 2015 Facebook, Inc.
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
#pragma once

#ifdef _MSC_VER
#include <folly/Portability.h>

#ifndef __STDC__
#define __STDC__ 1
#include <io.h>
#undef __STDC__
#else
#include <io.h>
#endif

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <folly/WindowsPortability.h>

// Next up, we define our own version of the normal posix
// names, to prevent windows from being annoying about it's
// "standards compliant" names. This also makes it possible
// to hook into these functions to deal with things like sockets.

#define F_OK 00
#define X_OK F_OK
#define W_OK 02
#define R_OK 04
#define RW_OK 06
#define HAVE_MODE_T 1
typedef unsigned short mode_t;

#define PATH_MAX MAX_PATH
#define MAXPATHLEN MAX_PATH

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define	LOCK_SH	1
#define	LOCK_EX	2
#define	LOCK_NB	4
#define	LOCK_UN	8

#define UIO_MAXIOV 16
#define IOV_MAX UIO_MAXIOV
struct iovec
{
  void *iov_base;
  size_t iov_len;
};


// We implement preadv and pwritev for windows.
#ifdef FOLLY_HAVE_PREADV
#undef FOLLY_HAVE_PREADV
#endif
#define FOLLY_HAVE_PREADV 1

#ifdef FOLLY_HAVE_PWRITEV
#undef FOLLY_HAVE_PWRITEV
#endif
#define FOLLY_HAVE_PWRITEV 1

#ifdef HAVE_POSIX_FALLOCATE
#undef HAVE_POSIX_FALLOCATE
#endif
#define HAVE_POSIX_FALLOCATE 1

#include <folly/SocketPortability.h>

// A few posix functions implemented for windows.
// These are written for functionality first, and
// spead / error handling second. (they don't translate
// the windows error codes to errno codes)

namespace folly { namespace file_portability {
bool is_fh_socket(int fh);
int access(char const* fn, int am);
int chmod(char const* fn, int am);
int chsize(int fh, long sz);
int close(int fh);
int creat(char const* fn, int pm);
int dup(int fh);
int dup2(int fhs, int fhd);
int eof(int fh);
long filelength(int fh);
int getdtablesize();

// I have no idea what the normal values for these are,
// and really don't care what they are. They're only used
// within fcntl, so it's not an issue.
#define FD_CLOEXEC HANDLE_FLAG_INHERIT
#define O_NONBLOCK 1
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
int fcntl(int fd, int cmd, ...);
int flock(int fd, int operation);
int fsync(int fd);
int ftruncate(int fd, off_t len);
int isatty(int fh);
int locking(int fh, int lm, long nb);
long lseek(int fh, long off, int orig);

#define S_ISLNK(a) (false)
#define S_ISDIR(a) ((a & _S_IFDIR) == _S_IFDIR)
#define S_ISREG(a) ((a & _S_IFREG) == _S_IFREG)
#define S_ISSOCK(a) (::folly::file_portability::is_fh_socket(a))
#define MAXSYMLINKS 255
int lstat(const char* path, struct stat* st);
char* mkdtemp(char* tn);
int mkstemp(char* tn);
char* mktemp(char* tn);
int open(char const* fn, int of, int pm = 0);
int pipe(int* pth);
int posix_fallocate(int fd, off_t offset, off_t len);
int pread(int fd, void* buf, size_t count, off_t offset);
ssize_t preadv(int fd, const iovec* iov, int count, off_t offset);
int pwrite(int fd, const void* buf, size_t count, off_t offset);
ssize_t pwritev(int fd, const iovec* iov, int count, off_t offset);
int read(int fh, void* buf, unsigned int mcc);
ssize_t readlink(const char* path, char* buf, size_t buflen);
ssize_t readv(int fd, const iovec* iov, int count);
char* realpath(const char* path, char* resolved_path);
int setmode(int fh, int md);
int sopen(char const* fn, int of, int sf, int pm = 0);
long tell(int fh);
int truncate(const char* path, off_t len);
int umask(int md);
int write(int fh, void const* buf, unsigned int mcc);
ssize_t writev(int fd, const iovec* iov, int count);
}}

using namespace folly::file_portability;
#else
#include <unistd.h>
#include <sys/file.h>
#include <sys/uio.h>
#endif
