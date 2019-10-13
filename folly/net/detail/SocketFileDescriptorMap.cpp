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

#include <folly/net/detail/SocketFileDescriptorMap.h>

#ifdef _WIN32

#include <shared_mutex>
#include <unordered_map>

#include <fcntl.h>

// We need this, but it's only defined for kernel drivers :(
#define STATUS_HANDLE_NOT_CLOSABLE 0xC0000235L

namespace folly {
namespace netops {
namespace detail {

static std::unordered_map<SOCKET, int> socketMap;
static std::shared_mutex socketMapMutex;

static int closeOnlyFileDescriptor(int fd) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);

  // If we were to just call _close on the descriptor, it would
  // close the HANDLE, but it wouldn't free any of the resources
  // associated to the SOCKET, and we can't call _close after
  // calling closesocket, because closesocket has already closed
  // the HANDLE, and _close would attempt to close the HANDLE
  // again, resulting in a double free.
  // We can however protect the HANDLE from actually being closed
  // long enough to close the file descriptor, then close the
  // socket itself.
  constexpr DWORD protectFlag = HANDLE_FLAG_PROTECT_FROM_CLOSE;
  DWORD handleFlags = 0;
  if (!GetHandleInformation(h, &handleFlags)) {
    return -1;
  }
  if (!SetHandleInformation(h, protectFlag, protectFlag)) {
    return -1;
  }
  int c = 0;
  __try {
    // We expect this to fail. It still closes the file descriptor though.
    c = ::_close(fd);
    // We just have to catch the SEH exception that gets thrown when we do
    // this with a debugger attached -_-....
  } __except (
      GetExceptionCode() == STATUS_HANDLE_NOT_CLOSABLE
          ? EXCEPTION_CONTINUE_EXECUTION
          : EXCEPTION_CONTINUE_SEARCH) {
    // We told it to continue execution, so nothing here would
    // be run anyways.
  }
  // We're at the core, we don't get the luxery of SCOPE_EXIT because
  // of circular dependencies.
  if (!SetHandleInformation(h, protectFlag, handleFlags)) {
    return -1;
  }
  if (c != -1) {
    return -1;
  }
  return 0;
}

int SocketFileDescriptorMap::close(int fd) noexcept {
  auto hand = SocketFileDescriptorMap::fdToSocket(fd);
  {
    std::unique_lock<std::shared_mutex> lock{socketMapMutex};
    if (socketMap.find(hand) != socketMap.end()) {
      socketMap.erase(hand);
    }
  }
  auto r = closeOnlyFileDescriptor(fd);
  if (r != 0) {
    return r;
  }
  return closesocket((SOCKET)hand);
}

int SocketFileDescriptorMap::close(SOCKET sock) noexcept {
  bool found = false;
  int fd = 0;
  {
    std::unique_lock<std::shared_mutex> lock{socketMapMutex};
    auto lookup = socketMap.find(sock);
    found = lookup != socketMap.end();
    if (found) {
      fd = lookup->second;
    }
  }

  if (found) {
    return SocketFileDescriptorMap::close(fd);
  }

  return closesocket(sock);
}

SOCKET SocketFileDescriptorMap::fdToSocket(int fd) noexcept {
  if (fd == -1) {
    return INVALID_SOCKET;
  }

  return (SOCKET)_get_osfhandle(fd);
}

int SocketFileDescriptorMap::socketToFd(SOCKET sock) noexcept {
  if (sock == INVALID_SOCKET) {
    return -1;
  }

  {
    std::shared_lock<std::shared_mutex> lock{socketMapMutex};
    auto const found = socketMap.find(sock);
    if (found != socketMap.end()) {
      return found->second;
    }
  }

  std::unique_lock<std::shared_mutex> lock{socketMapMutex};
  auto const found = socketMap.find(sock);
  if (found != socketMap.end()) {
    return found->second;
  }

  int fd = _open_osfhandle((intptr_t)sock, O_RDWR | O_BINARY);
  socketMap.emplace(sock, fd);
  return fd;
}
} // namespace detail
} // namespace netops
} // namespace folly
#endif
