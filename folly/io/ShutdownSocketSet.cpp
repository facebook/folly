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

#include <folly/io/ShutdownSocketSet.h>

#include <chrono>
#include <thread>

#include <glog/logging.h>

#include <folly/FileUtil.h>
#include <folly/net/NetOps.h>
#include <folly/portability/Sockets.h>

namespace folly {

using handle_t = NetworkSocket::native_handle_type;
static constexpr auto handle_tag = std::is_integral<handle_t>{};
static constexpr auto invalid_pos = ~size_t(0);

template <typename Cap>
static size_t cap_(std::false_type, Cap) {
  return 0;
}
template <typename Cap, typename H = handle_t>
static size_t cap_(std::true_type, Cap cap) {
  cap = cap == invalid_pos ? 0 : cap;
  auto max = static_cast<Cap>(std::numeric_limits<H>::max());
  return cap > max ? max : cap;
}
static size_t cap(size_t capacity) {
  return cap_(handle_tag, capacity);
}

template <typename Data>
static size_t pos_(std::false_type, Data) {
  return invalid_pos;
}
template <typename Data>
static size_t pos_(std::true_type, Data data) {
  return to_unsigned(data);
}
static size_t pos(NetworkSocket fd) {
  return fd.data == NetworkSocket::invalid_handle_value
      ? invalid_pos
      : pos_(handle_tag, fd.data);
}

template <typename Pos>
static NetworkSocket at_(std::false_type, Pos) {
  return NetworkSocket();
}
template <typename Pos>
static NetworkSocket at_(std::true_type, Pos p) {
  return NetworkSocket(static_cast<NetworkSocket::native_handle_type>(p));
}
static NetworkSocket at(size_t p) {
  return p == invalid_pos ? NetworkSocket() : at_(handle_tag, p);
}

ShutdownSocketSet::ShutdownSocketSet(size_t capacity)
    : capacity_(cap(capacity)),
      data_(static_cast<relaxed_atomic<uint8_t>*>(
          folly::checkedCalloc(capacity_, sizeof(relaxed_atomic<uint8_t>)))),
      nullFile_("/dev/null", O_RDWR) {}

void ShutdownSocketSet::add(NetworkSocket fd) {
  // Silently ignore any fds >= capacity_, very unlikely
  DCHECK_NE(fd, NetworkSocket());
  auto p = pos(fd);
  if (p >= capacity_) {
    return;
  }

  auto& sref = data_[p];
  uint8_t prevState = FREE;
  CHECK(sref.compare_exchange_strong(prevState, IN_USE))
      << "Invalid prev state for fd " << fd << ": " << int(prevState);
}

void ShutdownSocketSet::remove(NetworkSocket fd) {
  DCHECK_NE(fd, NetworkSocket());
  auto p = pos(fd);
  if (p >= capacity_) {
    return;
  }

  auto& sref = data_[p];
  uint8_t prevState = 0;

  prevState = sref.load();
  do {
    switch (prevState) {
      case IN_SHUTDOWN:
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        prevState = sref.load();
        continue;
      case FREE:
        LOG(FATAL) << "Invalid prev state for fd " << fd << ": "
                   << int(prevState);
    }
  } while (!sref.compare_exchange_weak(prevState, FREE));
}

int ShutdownSocketSet::close(NetworkSocket fd) {
  DCHECK_NE(fd, NetworkSocket());
  auto p = pos(fd);
  if (p >= capacity_) {
    return folly::closeNoInt(fd);
  }

  auto& sref = data_[p];
  uint8_t prevState = sref.load();
  uint8_t newState = 0;

  do {
    switch (prevState) {
      case IN_USE:
      case SHUT_DOWN:
        newState = FREE;
        break;
      case IN_SHUTDOWN:
        newState = MUST_CLOSE;
        break;
      default:
        LOG(FATAL) << "Invalid prev state for fd " << fd << ": "
                   << int(prevState);
    }
  } while (!sref.compare_exchange_strong(prevState, newState));

  return newState == FREE ? folly::closeNoInt(fd) : 0;
}

void ShutdownSocketSet::shutdown(NetworkSocket fd, bool abortive) {
  DCHECK_NE(fd, NetworkSocket());
  auto p = pos(fd);
  if (p >= capacity_) {
    doShutdown(fd, abortive);
    return;
  }

  auto& sref = data_[p];
  uint8_t prevState = IN_USE;
  if (!sref.compare_exchange_strong(prevState, IN_SHUTDOWN)) {
    return;
  }

  doShutdown(fd, abortive);

  prevState = IN_SHUTDOWN;
  if (sref.compare_exchange_strong(prevState, SHUT_DOWN)) {
    return;
  }

  CHECK_EQ(prevState, MUST_CLOSE)
      << "Invalid prev state for fd " << fd << ": " << int(prevState);

  folly::closeNoInt(fd); // ignore errors, nothing to do

  CHECK(sref.compare_exchange_strong(prevState, FREE))
      << "Invalid prev state for fd " << fd << ": " << int(prevState);
}

void ShutdownSocketSet::shutdownAll(bool abortive) {
  for (size_t p = 0; p < capacity_; ++p) {
    auto& sref = data_[p];
    if (sref.load() == IN_USE) {
      shutdown(at(p), abortive);
    }
  }
}

void ShutdownSocketSet::doShutdown(NetworkSocket fd, bool abortive) {
  // shutdown() the socket first, to awaken any threads blocked on the fd
  // (subsequent IO will fail because it's been shutdown); close()ing the
  // socket does not wake up blockers, see
  // http://stackoverflow.com/a/3624545/1736339
  folly::shutdownNoInt(fd, SHUT_RDWR);

  // If abortive shutdown is desired, we'll set the SO_LINGER option on
  // the socket with a timeout of 0; this will cause RST to be sent on
  // close.
  if (abortive) {
    struct linger l = {1, 0};
    if (netops::setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) != 0) {
      // Probably not a socket, ignore.
      return;
    }
  }

  // Note that this can be ignored without breaking anything due to the
  // fact the file descriptor will eventually be closed, so on Windows
  // all of the socket resources will just stick around longer than they
  // do on posix-like systems.
#ifndef _WIN32
  // We can't close() the socket, as that would be dangerous; a new file
  // could be opened and get the same file descriptor, and then code assuming
  // the old fd would do IO in the wrong place. We'll (atomically) dup2
  // /dev/null onto the fd instead.
  folly::dup2NoInt(nullFile_.fd(), fd.toFd());
#endif
}

} // namespace folly
