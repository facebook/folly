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

#ifdef _MSC_VER
#include <folly/FilePortability.h>

namespace folly { namespace file_portability {

bool is_fh_socket(int fh) {
  SOCKET h = ::fsp::fd_to_socket(fh);
  WSANETWORKEVENTS e;
  e.lNetworkEvents = 0xABCDEF12;
  WSAEnumNetworkEvents(h, nullptr, &e);
  return e.lNetworkEvents != 0xABCDEF12;
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

int access(char const* fn, int am) { 
  return _access(fn, am);
}

int chmod(char const* fn, int am) { 
  return _chmod(fn, am);
}

int chsize(int fh, long sz) {
  return _chsize(fh, sz);
}

int close(int fh) {
  if (folly::file_portability::is_fh_socket(fh)) {
    SOCKET h = (SOCKET)_get_osfhandle(fh);
    return closesocket(h);
  }
  return _close(fh);
}

int creat(char const* fn, int pm) {
  return _creat(fn, pm);
}

int dup(int fh) {
  return _dup(fh);
}

int dup2(int fhs, int fhd) {
  return _dup2(fhs, fhd);
}

int eof(int fh) {
  return _eof(fh);
}

long filelength(int fh) {
  return _filelength(fh); 
}

int getdtablesize() {
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
int fcntl(int fd, int cmd, ...) {
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
        if (SetHandleInformation(h, HANDLE_FLAG_INHERIT,
                                 (DWORD)(flags & FD_CLOEXEC)))
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
        // If it's not a socket, it's probably a pipe, and
        // those are non-blocking by default with Windows.
        if (is_fh_socket(fd)) {
          SOCKET s = (SOCKET)_get_osfhandle(fd);
          if (s != INVALID_SOCKET) {
            u_long nonBlockingEnabled = 1;
            res = ioctlsocket(s, FIONBIO, &nonBlockingEnabled);
          }
        } else {
          res = 0;
        }
      }
      break;
    }
  }
  va_end(args);
  return res;
}

int flock(int fd, int operation) {
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
    if (!LockFileEx(h, flags, 0, fileSize.LowPart,
                    (DWORD)fileSize.HighPart, &ov))
      return -1;
  }
  return 0;
}

int fsync(int fd) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE)
    return -1;
  if (!FlushFileBuffers(h))
    return -1;
  return 0;
}

int ftruncate(int fd, off_t len) {
  if (_lseek(fd, len, SEEK_SET))
    return -1;

  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE)
    return -1;
  if (!SetEndOfFile(h))
    return -1;
  return 0;
}

int isatty(int fh) {
  return _isatty(fh);
}

int locking(int fh, int lm, long nb) {
  return _locking(fh, lm, nb);
}

long lseek(int fh, long off, int orig) {
  return _lseek(fh, off, orig);
}

#define S_ISLNK(a) (false)
#define S_ISDIR(a) ((a & _S_IFDIR) == _S_IFDIR)
#define S_ISREG(a) ((a & _S_IFREG) == _S_IFREG)
#define S_ISSOCK(a) (::folly::file_portability::is_fh_socket(a))
#define MAXSYMLINKS 255
int lstat(const char* path, struct stat* st) {
  return stat(path, st);
}

// While yes, this is for a directory, due to this being windows,
// a file and directory can't have the same name, resulting in this
// still working just fine.
char* mkdtemp(char* tn) {
  return _mktemp(tn);
}

int mkstemp(char* tn) {
  char *ptr = mktemp(tn);
  return open(ptr, O_RDWR | O_TRUNC | O_EXCL | O_CREAT, 0600);
}

char* mktemp(char* tn) { 
  return _mktemp(tn);
}

int open(char const* fn, int of, int pm) {
  int fh;
  errno_t res = _sopen_s(&fh, fn, of, _SH_DENYNO, pm);
  return res ? -1 : fh;
}

int pipe(int* pth) {
  return _pipe(pth, 0, O_BINARY);
}

int posix_fallocate(int fd, off_t offset, off_t len) {
  // We'll pretend we always have enough space. We
  // can't exactly pre-allocate on windows anyways.
  return 0;
}

int pread(int fd, void* buf, size_t count, off_t offset) {
  return wrapPositional(_read, fd, offset, buf, (unsigned int)count);
}

ssize_t preadv(int fd, const iovec* iov, int count, off_t offset) {
  return wrapPositional(readv, fd, offset, iov, count);
}

int pwrite(int fd, const void* buf, size_t count, off_t offset) {
  return wrapPositional(_write, fd, offset, buf, (unsigned int)count);
}

ssize_t pwritev(int fd, const iovec* iov, int count, off_t offset) {
  return wrapPositional(writev, fd, offset, iov, count);
}

int read(int fh, void* buf, unsigned int mcc) {
  return _read(fh, buf, mcc);
}

ssize_t readlink(const char* path, char* buf, size_t buflen) {
  if (!buflen)
    return -1;

  HMODULE lib = LoadLibrary("kernel32.dll");
  if (!lib)
    return -1;

  auto pFunc = (DWORD (WINAPI*)(HANDLE, LPTSTR, DWORD, DWORD))
    GetProcAddress(lib, "GetFinalPathNameByHandleA");
  if (pFunc == nullptr)
    return -1;

  HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return -1;

  DWORD ret = pFunc(h, buf, buflen - 1, VOLUME_NAME_DOS);
  if (ret >= buflen || ret >= MAXPATHLEN || !ret) {
    CloseHandle(h);
    return -1;
  }

  CloseHandle(h);
  buf[ret] = '\0';
  return ret;
}

ssize_t readv(int fd, const iovec* iov, int count) {
  ssize_t bytesRead = 0;
  for (int i = 0; i < count; i++) {
    int res = read(fd, iov[i].iov_base, (unsigned int)iov[i].iov_len);
    if (res != iov[i].iov_len)
      return -1;
    bytesRead += res;
  }
  return bytesRead;
}

char* realpath(const char* path, char* resolved_path) {
  // I sure hope the caller gave us _MAX_PATH space in the buffer....
  return _fullpath(resolved_path, path, _MAX_PATH);
}

int setmode(int fh, int md) { 
  return _setmode(fh, md);
}

int sopen(char const* fn, int of, int sf, int pm) {
  int fh;
  errno_t res = _sopen_s(&fh, fn, of, sf, pm);
  return res ? -1 : fh;
}

long tell(int fh) {
  return _tell(fh);
}

int truncate(const char* path, off_t len) {
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

int umask(int md) {
  return _umask(md);
}

int write(int fh, void const* buf, unsigned int mcc) {
  return _write(fh, buf, mcc);
}

ssize_t writev(int fd, const iovec* iov, int count) {
  ssize_t bytesWritten = 0;
  for (int i = 0; i < count; i++) {
    int res = write(fd, iov[i].iov_base, (unsigned int)iov[i].iov_len);
    if (res != iov[i].iov_len)
      return -1;
    bytesWritten += res;
  }
  return bytesWritten;
}

}}

#endif
