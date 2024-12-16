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

// We need to prevent winnt.h from defining the core STATUS codes,
// otherwise they will conflict with what we're getting from ntstatus.h
#define UMDF_USING_NTSTATUS

#include <folly/ScopeGuard.h>
#include <folly/portability/Unistd.h>

#if defined(__APPLE__)
off64_t lseek64(int fh, off64_t off, int orig) {
  return lseek(fh, off, orig);
}

ssize_t pread64(int fd, void* buf, size_t count, off64_t offset) {
  return pread(fd, buf, count, offset);
}

static_assert(
    sizeof(off_t) >= 8, "We expect that Mac OS have at least a 64-bit off_t.");
#endif

#ifdef _WIN32

#include <cstdio>

#include <fcntl.h>

#include <folly/net/detail/SocketFileDescriptorMap.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Windows.h>

#include <tlhelp32.h> // @manual

template <bool is64Bit, class Offset>
static Offset seek(int fd, Offset offset, int whence) {
  Offset res;
  if (is64Bit) {
    res = lseek64(fd, offset, whence);
  } else {
    res = lseek(fd, offset, whence);
  }
  return res;
}

// Generic wrapper for the p* family of functions.
template <bool is64Bit, class F, class Offset, class... Args>
static int wrapPositional(F f, int fd, Offset offset, Args... args) {
  Offset origLoc = seek<is64Bit>(fd, 0, SEEK_CUR);
  if (origLoc == Offset(-1)) {
    return -1;
  }

  Offset moved = seek<is64Bit>(fd, offset, SEEK_SET);
  if (moved == Offset(-1)) {
    return -1;
  }

  int res = (int)f(fd, args...);

  int curErrNo = errno;
  Offset afterOperation = seek<is64Bit>(fd, origLoc, SEEK_SET);
  if (afterOperation == Offset(-1)) {
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

namespace {

struct UniqueHandleWrapper {
  UniqueHandleWrapper(HANDLE handle) : handle_(handle) {}

  HANDLE get() const { return handle_; }
  bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }

  UniqueHandleWrapper(UniqueHandleWrapper&& other) {
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
  }
  UniqueHandleWrapper& operator=(UniqueHandleWrapper&& other) {
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
    return *this;
  }

  UniqueHandleWrapper(const UniqueHandleWrapper& other) = delete;
  UniqueHandleWrapper& operator=(const UniqueHandleWrapper& other) = delete;

  ~UniqueHandleWrapper() {
    if (valid()) {
      CloseHandle(handle_);
    }
  }

 private:
  HANDLE handle_;
};

struct ProcessHandleWrapper {
  ProcessHandleWrapper(HANDLE handle)
      : ProcessHandleWrapper(UniqueHandleWrapper(handle)) {}
  ProcessHandleWrapper(UniqueHandleWrapper handle)
      : procHandle_(std::move(handle)) {
    id_ = procHandle_.valid() ? GetProcessId(procHandle_.get()) : 1;
  }
  pid_t id() const { return id_; }

 private:
  UniqueHandleWrapper procHandle_;
  pid_t id_;
};

int64_t getProcessStartTime(HANDLE processHandle) {
  FILETIME createTime;
  FILETIME exitTime;
  FILETIME kernelTime;
  FILETIME userTime;

  if (GetProcessTimes(
          processHandle, &createTime, &exitTime, &kernelTime, &userTime) == 0) {
    return -1; // failed to get process times
  }

  ULARGE_INTEGER ret;
  ret.LowPart = createTime.dwLowDateTime;
  ret.HighPart = createTime.dwHighDateTime;

  return ret.QuadPart;
}

ProcessHandleWrapper getParentProcessHandle() {
  DWORD ppid = 1;
  DWORD pid = GetCurrentProcessId();

  UniqueHandleWrapper hSnapshot =
      CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (!hSnapshot.valid()) {
    return INVALID_HANDLE_VALUE;
  }

  PROCESSENTRY32 pe32;
  ZeroMemory(&pe32, sizeof(pe32));
  pe32.dwSize = sizeof(pe32);
  if (!Process32First(hSnapshot.get(), &pe32)) {
    return INVALID_HANDLE_VALUE;
  }
  do {
    if (pe32.th32ProcessID == pid) {
      ppid = pe32.th32ParentProcessID;
      break;
    }
  } while (Process32Next(hSnapshot.get(), &pe32));

  UniqueHandleWrapper parent = OpenProcess(PROCESS_ALL_ACCESS, false, ppid);
  if (!parent.valid()) {
    return INVALID_HANDLE_VALUE;
  }

  // Checking time of parent and child processes.
  // This is a sanity check in case we query for parent process id
  // after the parent of this process stopped already and something else
  // used it PID.
  // We need this logic because we can't guarantee that parent id hasn't been
  // reused before getting process handle to this process.

  int64_t currentProcessStartTime = getProcessStartTime(GetCurrentProcess());
  int64_t parentProcessStartTime = getProcessStartTime(parent.get());
  if (currentProcessStartTime == -1 || parentProcessStartTime == -1 ||
      currentProcessStartTime < parentProcessStartTime) {
    // Can't ensure process is still the same process as parent
    return INVALID_HANDLE_VALUE;
  }
  return std::move(parent);
}
} // namespace

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
  off_t origLoc = _lseek(fd, 0, SEEK_CUR);
  if (origLoc == -1) {
    return -1;
  }
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
  if (_lseek(fd, origLoc, SEEK_SET) == -1) {
    return -1;
  }
  return 0;
}

int getdtablesize() {
  return _getmaxstdio();
}

gid_t getgid() {
  return 1;
}

pid_t getppid() {
  // ProcessHandleWrapper stores Parent Process Handle inside
  // This means the parent PID is not going to be reused even if
  // parent process is no longer exists.
  static ProcessHandleWrapper wrapper = getParentProcessHandle();
  return wrapper.id();
}

uid_t getuid() {
  return 1;
}

int lockf(int fd, int cmd, off_t len) {
  return _locking(fd, cmd, len);
}

off64_t lseek64(int fh, off64_t off, int orig) {
  return _lseeki64(fh, static_cast<int64_t>(off), orig);
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
  const bool is64Bit = false;
  return wrapPositional<is64Bit>(_read, fd, offset, buf, (unsigned int)count);
}

ssize_t pread64(int fd, void* buf, size_t count, off64_t offset) {
  const bool is64Bit = true;
  return wrapPositional<is64Bit>(_read, fd, offset, buf, (unsigned int)count);
}

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
  const bool is64Bit = false;
  return wrapPositional<is64Bit>(_write, fd, offset, buf, (unsigned int)count);
}

ssize_t readlink(const char* path, char* buf, size_t buflen) {
  if (!buflen) {
    return -1;
  }

  HANDLE h = CreateFileA(
      path,
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS,
      nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }

  DWORD ret =
      GetFinalPathNameByHandleA(h, buf, DWORD(buflen - 1), VOLUME_NAME_DOS);
  if (ret >= buflen || ret >= MAX_PATH || !ret) {
    CloseHandle(h);
    return -1;
  }

  CloseHandle(h);
  buf[ret] = '\0';
  return ret;
}

void* sbrk(intptr_t /* i */) {
  return (void*)-1;
}

unsigned int sleep(unsigned int seconds) {
  Sleep((DWORD)(seconds * 1000));
  return 0;
}

long sysconf(int tp) {
  switch (tp) {
    case _SC_PAGESIZE: {
      SYSTEM_INFO inf;
      GetSystemInfo(&inf);
      return (long)inf.dwPageSize;
    }
    case _SC_NPROCESSORS_ONLN: {
      SYSTEM_INFO inf;
      GetSystemInfo(&inf);
      return (long)inf.dwNumberOfProcessors;
    }
    default:
      return -1L;
  }
}

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
} // namespace unistd
} // namespace portability

namespace fileops {
int close(int fh) {
  if (folly::portability::sockets::is_fh_socket(fh)) {
    return netops::detail::SocketFileDescriptorMap::close(fh);
  }
  return _close(fh);
}

ssize_t read(int fh, void* buf, size_t count) {
  if (folly::portability::sockets::is_fh_socket(fh)) {
    SOCKET s = (SOCKET)_get_osfhandle(fh);
    if (s != INVALID_SOCKET) {
      auto r = folly::portability::sockets::recv(fh, buf, count, 0);
      if (r == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
        errno = EAGAIN;
      }
      return r;
    }
  }
  auto r = _read(fh, buf, static_cast<unsigned int>(count));
  if (r == -1 && GetLastError() == ERROR_NO_DATA) {
    // This only happens if the file was non-blocking and
    // no data was present. We have to translate the error
    // to a form that the rest of the world is expecting.
    errno = EAGAIN;
  }
  return r;
}

int pipe(int pth[2]) {
  // We need to be able to listen to pipes with
  // libevent, so they need to be actual sockets.
  return socketpair(PF_UNIX, SOCK_STREAM, 0, pth);
}

ssize_t write(int fh, void const* buf, size_t count) {
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
  auto r = _write(fh, buf, static_cast<unsigned int>(count));
  if ((r > 0 && size_t(r) != count) || (r == -1 && errno == ENOSPC)) {
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
} // namespace fileops
} // namespace folly

#endif
