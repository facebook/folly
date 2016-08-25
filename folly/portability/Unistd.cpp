/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/portability/Unistd.h>

#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Windows.h>

// Generic wrapper for the p* family of functions.
template <class F, class... Args>
static int wrapPositional(F f, int fd, off_t offset, Args... args) {
  off_t origLoc = lseek(fd, 0, SEEK_CUR);
  if (origLoc == (off_t)-1) {
    return -1;
  }
  if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
    return -1;
  }

  int res = (int)f(fd, args...);

  int curErrNo = errno;
  if (lseek(fd, origLoc, SEEK_SET) == (off_t)-1) {
    if (res == -1) {
      errno = curErrNo;
    }
    return -1;
  }
  errno = curErrNo;

  return res;
}

namespace folly {
namespace portability {
namespace unistd {
int access(char const* fn, int am) { return _access(fn, am); }

int chdir(const char* path) { return _chdir(path); }

// We aren't hooking into the internals of the CRT, nope, not at all.
extern "C" int __cdecl _free_osfhnd(int const fh);
int close(int fh) {
  if (folly::portability::sockets::is_fh_socket(fh)) {
    SOCKET h = (SOCKET)_get_osfhandle(fh);

    // If we were to just call _close on the descriptor, it would
    // close the HANDLE, but it wouldn't free any of the resources
    // associated to the SOCKET, and we can't call _close after
    // calling closesocket, because closesocket has already closed
    // the HANDLE, and _close would attempt to close the HANDLE
    // again, resulting in a double free.
    // Luckily though, there is a function in the internals of the
    // CRT that is used to free only the file descriptor, so we
    // can call that to avoid leaking the file descriptor itself.
    auto c = closesocket(h);
    _free_osfhnd(fh);
    return c;
  }
  return _close(fh);
}

int dup(int fh) { return _dup(fh); }

int dup2(int fhs, int fhd) { return _dup2(fhs, fhd); }

int fsync(int fd) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }
  if (!FlushFileBuffers(h)) {
    return -1;
  }
  return 0;
}

int ftruncate(int fd, off_t len) {
  if (_lseek(fd, len, SEEK_SET) == -1) {
    return -1;
  }

  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }
  if (!SetEndOfFile(h)) {
    return -1;
  }
  return 0;
}

char* getcwd(char* buf, int sz) { return _getcwd(buf, sz); }

int getdtablesize() { return _getmaxstdio(); }

int getgid() { return 1; }

pid_t getpid() { return pid_t(GetCurrentProcessId()); }

// No major need to implement this, and getting a non-potentially
// stale ID on windows is a bit involved.
pid_t getppid() { return (pid_t)1; }

int getuid() { return 1; }

int isatty(int fh) { return _isatty(fh); }

int lockf(int fd, int cmd, off_t len) { return _locking(fd, cmd, len); }

long lseek(int fh, long off, int orig) { return _lseek(fh, off, orig); }

int rmdir(const char* path) { return _rmdir(path); }

int pipe(int pth[2]) {
  // We need to be able to listen to pipes with
  // libevent, so they need to be actual sockets.
  return socketpair(PF_UNIX, SOCK_STREAM, 0, pth);
}

int pread(int fd, void* buf, size_t count, off_t offset) {
  return wrapPositional(_read, fd, offset, buf, (unsigned int)count);
}

int pwrite(int fd, const void* buf, size_t count, off_t offset) {
  return wrapPositional(_write, fd, offset, buf, (unsigned int)count);
}

int read(int fh, void* buf, unsigned int mcc) {
  if (folly::portability::sockets::is_fh_socket(fh)) {
    SOCKET s = (SOCKET)_get_osfhandle(fh);
    if (s != INVALID_SOCKET) {
      auto r = folly::portability::sockets::recv(fh, buf, (size_t)mcc, 0);
      if (r == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
        errno = EAGAIN;
      }
      return r;
    }
  }
  auto r = _read(fh, buf, mcc);
  if (r == -1 && GetLastError() == ERROR_NO_DATA) {
    // This only happens if the file was non-blocking and
    // no data was present. We have to translate the error
    // to a form that the rest of the world is expecting.
    errno = EAGAIN;
  }
  return r;
}

ssize_t readlink(const char* path, char* buf, size_t buflen) {
  if (!buflen) {
    return -1;
  }

  HANDLE h = CreateFile(path,
                        GENERIC_READ,
                        FILE_SHARE_READ,
                        nullptr,
                        OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS,
                        nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }

  DWORD ret = GetFinalPathNameByHandleA(h, buf, buflen - 1, VOLUME_NAME_DOS);
  if (ret >= buflen || ret >= MAX_PATH || !ret) {
    CloseHandle(h);
    return -1;
  }

  CloseHandle(h);
  buf[ret] = '\0';
  return ret;
}

void* sbrk(intptr_t i) { return (void*)-1; }

int setmode(int fh, int md) { return _setmode(fh, md); }

unsigned int sleep(unsigned int seconds) {
  Sleep((DWORD)(seconds * 1000));
  return 0;
}

size_t sysconf(int tp) {
  switch (tp) {
    case _SC_PAGESIZE: {
      SYSTEM_INFO inf;
      GetSystemInfo(&inf);
      return (size_t)inf.dwPageSize;
    }
    case _SC_NPROCESSORS_ONLN: {
      SYSTEM_INFO inf;
      GetSystemInfo(&inf);
      return (size_t)inf.dwNumberOfProcessors;
    }
    default:
      return (size_t)-1;
  }
}

long tell(int fh) { return _tell(fh); }

int truncate(const char* path, off_t len) {
  int fd = _open(path, O_WRONLY);
  if (!fd) {
    return -1;
  }
  if (ftruncate(fd, len)) {
    _close(fd);
    return -1;
  }
  return _close(fd) ? -1 : 0;
}

int usleep(unsigned int ms) {
  Sleep((DWORD)(ms / 1000));
  return 0;
}

int write(int fh, void const* buf, unsigned int count) {
  if (folly::portability::sockets::is_fh_socket(fh)) {
    SOCKET s = (SOCKET)_get_osfhandle(fh);
    if (s != INVALID_SOCKET) {
      auto r = folly::portability::sockets::send(fh, buf, (size_t)count, 0);
      if (r == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
        errno = EAGAIN;
      }
      return r;
    }
  }
  auto r = _write(fh, buf, count);
  if ((r > 0 && r != count) || (r == -1 && errno == ENOSPC)) {
    // Writing to a pipe with a full buffer doesn't generate
    // any error type, unless it caused us to write exactly 0
    // bytes, so we have to see if we have a pipe first. We
    // don't touch the errno for anything else.
    HANDLE h = (HANDLE)_get_osfhandle(fh);
    if (GetFileType(h) == FILE_TYPE_PIPE) {
      DWORD state = 0;
      if (GetNamedPipeHandleState(
              h, &state, nullptr, nullptr, nullptr, nullptr, 0)) {
        if ((state & PIPE_NOWAIT) == PIPE_NOWAIT) {
          errno = EAGAIN;
          return -1;
        }
      }
    }
  }
  return r;
}
}
}
}
#endif
