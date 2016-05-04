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

#include <folly/portability/Fcntl.h>

#ifdef _WIN32
#include <folly/portability/Sockets.h>
#include <folly/portability/Windows.h>

namespace folly {
namespace portability {
namespace fcntl {
int creat(char const* fn, int pm) { return _creat(fn, pm); }

int fcntl(int fd, int cmd, ...) {
  va_list args;
  int res = -1;
  va_start(args, cmd);
  switch (cmd) {
    case F_GETFD: {
      HANDLE h = (HANDLE)_get_osfhandle(fd);
      if (h != INVALID_HANDLE_VALUE) {
        DWORD flags;
        if (GetHandleInformation(h, &flags)) {
          res = flags & HANDLE_FLAG_INHERIT;
        }
      }
      break;
    }
    case F_SETFD: {
      int flags = va_arg(args, int);
      HANDLE h = (HANDLE)_get_osfhandle(fd);
      if (h != INVALID_HANDLE_VALUE) {
        if (SetHandleInformation(
                h, HANDLE_FLAG_INHERIT, (DWORD)(flags & FD_CLOEXEC))) {
          res = 0;
        }
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
        if (folly::portability::sockets::is_fh_socket(fd)) {
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

int open(char const* fn, int of, int pm) {
  int fh;
  errno_t res = _sopen_s(&fh, fn, of, _SH_DENYNO, pm);
  return res ? -1 : fh;
}

int posix_fallocate(int fd, off_t offset, off_t len) {
  // We'll pretend we always have enough space. We
  // can't exactly pre-allocate on windows anyways.
  return 0;
}
}
}
}
#endif
