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

#include <Windows.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifndef __STDC__
#define __STDC__ 1
#include <io.h>
#undef __STDC__
#else
#include <io.h>
#endif

// Next up, we define our own version of the normal posix
// names, to prevent windows from being annoying about it's
// "standards compliant" names. This also makes it possible
// to hook into these functions to deal with things like sockets.

#define F_OK 00
#define X_OK F_OK
#define W_OK 02
#define R_OK 04
#define RW_OK 06
inline int __cdecl access(char const* fn, int am) { return _access(fn, am); }
inline int __cdecl chmod(char const* fn, int am) { return _chmod(fn, am); }
inline int __cdecl chsize(int fh, long sz) { return _chsize(fh, sz); }
inline int __cdecl creat(char const* fn, int pm) { return _creat(fn, pm); }
inline int __cdecl dup(int fh) { return _dup(fh); }
inline int __cdecl dup2(int fhs, int fhd) { return _dup2(fhs, fhd); }
inline int __cdecl eof(int fh) { return _eof(fh); }
inline long __cdecl filelength(int fh) { return _filelength(fh); }
inline int __cdecl isatty(int fh) { return _isatty(fh); }
inline int __cdecl locking(int fh, int lm, long nb) { return _locking(fh, lm, nb); }
inline long __cdecl lseek(int fh, long off, int orig) { return _lseek(fh, off, orig); }
inline char* __cdecl mktemp(char* tn) { return _mktemp(tn); }
// While yes, this is for a directory, due to this being windows,
// a file and directory can't have the same name, resulting in this
// still working just fine.
inline char* __cdecl mkdtemp(char* tn) { return _mktemp(tn); }
inline int __cdecl open(char const* fn, int of, int pm = 0) {
  int fh;
  errno_t res = _sopen_s(&fh, fn, of, _SH_DENYNO, pm);
  return res ? -1 : fh;
}
// Need open
inline int __cdecl mkstemp(char* tn) {
  return open(mktemp(tn), O_RDWR | O_EXCL);
}
inline int __cdecl pipe(int* pth) { return _pipe(pth, 0, O_BINARY); }
inline int __cdecl read(int fh, void* buf, unsigned int mcc) { return _read(fh, buf, mcc); }
inline int __cdecl setmode(int fh, int md) { return _setmode(fh, md); }
inline int __cdecl sopen(char const* fn, int of, int sf, int pm = 0) {
  int fh;
  errno_t res = _sopen_s(&fh, fn, of, sf, pm);
  return res ? -1 : fh;
}
inline long __cdecl tell(int fh) { return _tell(fh); }
inline int __cdecl umask(int md) { return _umask(md); }
inline int __cdecl unlink(char const* fn) { return _unlink(fn); }
inline int __cdecl write(int fh, void const* buf, unsigned int mcc) { return _write(fh, buf, mcc); }

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

#include <folly/SocketPortability.h>

// A few posix functions implemented for windows.
// These are written for functionality first, and
// spead / error handling second. (they don't translate
// the windows error codes to errno codes)

namespace folly { namespace file_portability {

inline bool is_fh_socket(int fh) {
  SOCKET h = (SOCKET)_get_osfhandle(fh);
  WSANETWORKEVENTS e;
  e.lNetworkEvents = 0xABCDEF12;
  WSAEnumNetworkEvents(h, nullptr, &e);
  return e.lNetworkEvents == 0xABCDEF12;
}

// Generic wrapper for the p* family of functions.
template<class F, class... Args>
int wrapPositional(F f, int fd, off_t offset, Args... args) {
  off_t origLoc = lseek(fd, 0, SEEK_CUR);
  if (origLoc == (off_t)-1)
    return -1;
  if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
    return -1;

  int res = (int)f(fd, args...);

  int curErrNo = errno;  
  if (lseek(fd, origLoc, SEEK_SET) == (off_t)-1) {
    if (res == -1)
      errno = curErrNo;
    return -1;
  }
  errno = curErrNo;

  return res;
}

}}

ssize_t readv(int fd, const iovec* iov, int count);
ssize_t writev(int fd, const iovec* iov, int count);

inline int __cdecl close(int fh) {
  if (folly::file_portability::is_fh_socket(fh)) {
    SOCKET h = (SOCKET)_get_osfhandle(fh);
    return closesocket(h);
  }
  return _close(fh);
}

inline int getdtablesize() {
  return _getmaxstdio();
}

// I have no idea what the normal values for these are,
// and really don't care what they are. They're only used
// within fcntl, so it's not an issue.
#define FD_CLOEXEC HANDLE_FLAG_INHERIT
#define O_NONBLOCK 1
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
inline int fcntl(int fd, int cmd, ...) {
  va_list args;
  int res = -1;
  va_start(args, cmd);
  switch (cmd) {
    case F_GETFD: {
      HANDLE h = (HANDLE)_get_osfhandle(fd);
      if (h != INVALID_HANDLE_VALUE) {
        DWORD flags;
        if (GetHandleInformation(h, &flags))
          res = flags & HANDLE_FLAG_INHERIT;
      }
      break;
    }
    case F_SETFD: {
      int flags = va_arg(args, int);
      HANDLE h = (HANDLE)_get_osfhandle(fd);
      if (h != INVALID_HANDLE_VALUE) {
        if (SetHandleInformation(h, HANDLE_FLAG_INHERIT, (DWORD)(flags & FD_CLOEXEC)))
          res = 0;
      }
      break;
    }
    case F_GETFL: {
      // No idea how to get the IO blocking mode, so return 0.
      res = 0;
      break;
    }
    case F_SETFL: {
      int flags = va_arg(args, int);
      if (flags & O_NONBLOCK) {
        SOCKET s = (SOCKET)_get_osfhandle(fd);
        if (s != INVALID_SOCKET) {
          u_long nonBlockingEnabled = 1;
          res = ioctlsocket(s, FIONBIO, &nonBlockingEnabled);
        }
      }
      break;
    }
  }
  va_end(args);
  return res;
}

inline int flock(int fd, int operation) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE)
    return -1;

  LARGE_INTEGER fileSize;
  if (!GetFileSizeEx(h, &fileSize))
    return -1;

  if (operation & LOCK_UN) {
    if (!UnlockFile(h, 0, 0, fileSize.LowPart, (DWORD)fileSize.HighPart))
      return -1;
  }
  else {
    int flags = 0;
    OVERLAPPED ov = {};
    if (operation & LOCK_NB)
      flags |= LOCKFILE_FAIL_IMMEDIATELY;
    if (operation & LOCK_EX)
      flags |= LOCKFILE_EXCLUSIVE_LOCK;
    if (!LockFileEx(h, flags, 0, fileSize.LowPart, (DWORD)fileSize.HighPart, &ov))
      return -1;
  }
  return 0;
}

inline int fsync(int fd) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE)
    return -1;
  if (!FlushFileBuffers(h))
    return -1;
  return 0;
}

inline int ftruncate(int fd, off_t len) {
  if (lseek(fd, len, SEEK_SET))
    return -1;

  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE)
    return -1;
  if (!SetEndOfFile(h))
    return -1;
  return 0;
}

#define S_ISLNK(a) (false)
#define S_ISDIR(a) ((a & _S_IFDIR) == _S_IFDIR)
#define S_ISREG(a) ((a & _S_IFREG) == _S_IFREG)
#define S_ISSOCK(a) (::folly::file_portability::is_fh_socket(a))
#define MAXSYMLINKS 255
inline int lstat(const char* path, struct stat* st) {
  return stat(path, st);
}

inline int pread(int fd, void* buf, size_t count, off_t offset) {
  return ::folly::file_portability::wrapPositional(read, fd, offset, buf, (unsigned int)count);
}

inline ssize_t preadv(int fd, const iovec* iov, int count, off_t offset) {
  return ::folly::file_portability::wrapPositional(readv, fd, offset, iov, count);
}

inline int pwrite(int fd, const void* buf, size_t count, off_t offset) {
  return ::folly::file_portability::wrapPositional(write, fd, offset, buf, (unsigned int)count);
}

inline ssize_t pwritev(int fd, const iovec* iov, int count, off_t offset) {
  return ::folly::file_portability::wrapPositional(writev, fd, offset, iov, count);
}

inline ssize_t readlink(const char* path, char* buf, size_t buflen) {
  size_t len = strlen(path);
  if (len <= buflen) {
    strncpy(buf, path, len);
    return len;
  }
  return 0;
}

inline ssize_t readv(int fd, const iovec* iov, int count) {
  ssize_t bytesRead = 0;
  for (int i = 0; i < count; i++) {
    int res = read(fd, iov[i].iov_base, (unsigned int)iov[i].iov_len);
    if (res != iov[i].iov_len)
      return -1;
    bytesRead += res;
  }
  return bytesRead;
}

inline char* realpath(const char* path, char* resolved_path) {
  // I sure hope the caller gave us _MAX_PATH space in the buffer....
  return _fullpath(resolved_path, path, _MAX_PATH);
}

inline int truncate(const char* path, off_t len) {
  int fd = open(path, O_WRONLY);
  if (!fd)
    return -1;
  if (ftruncate(fd, len))
  {
    close(fd);
    return -1;
  }
  if (close(fd))
    return -1;
  return 0;
}

inline ssize_t writev(int fd, const iovec* iov, int count) {
  ssize_t bytesWritten = 0;
  for (int i = 0; i < count; i++) {
    int res = write(fd, iov[i].iov_base, (unsigned int)iov[i].iov_len);
    if (res != iov[i].iov_len)
      return -1;
    bytesWritten += res;
  }
  return bytesWritten;
}

#else
#include <unistd.h>
#include <sys/file.h>
#include <sys/uio.h>
#endif