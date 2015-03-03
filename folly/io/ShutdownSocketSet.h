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

#include <atomic>
#include <cstdlib>
#include <memory>

#include <boost/noncopyable.hpp>

#include <folly/File.h>

namespace folly {

/**
 * Set of sockets that allows immediate, take-no-prisoners abort.
 */
class ShutdownSocketSet : private boost::noncopyable {
 public:
  /**
   * Create a socket set that can handle file descriptors < maxFd.
   * The default value (256Ki) is high enough for just about all
   * applications, even if you increased the number of file descriptors
   * on your system.
   */
  explicit ShutdownSocketSet(size_t maxFd = 1 << 18);

  /**
   * Add an already open socket to the list of sockets managed by
   * ShutdownSocketSet. You MUST close the socket by calling
   * ShutdownSocketSet::close (which will, as a side effect, also handle EINTR
   * properly) and not by calling close() on the file descriptor.
   */
  void add(int fd);

  /**
   * Remove a socket from the list of sockets managed by ShutdownSocketSet.
   * Note that remove() might block! (which we lamely implement by
   * sleep()-ing) in the extremely rare case that the fd is currently
   * being shutdown().
   */
  void remove(int fd);

  /**
   * Close a socket managed by ShutdownSocketSet. Returns the same return code
   * as ::close() (and sets errno accordingly).
   */
  int close(int fd);

  /**
   * Shut down a socket. If abortive is true, we perform an abortive
   * shutdown (send RST rather than FIN). Note that we might still end up
   * sending FIN due to the rather interesting implementation.
   *
   * This is async-signal-safe and ignores errors.  Obviously, subsequent
   * read() and write() operations to the socket will fail. During normal
   * operation, just call ::shutdown() on the socket.
   */
  void shutdown(int fd, bool abortive=false);

  /**
   * Shut down all sockets managed by ShutdownSocketSet. This is
   * async-signal-safe and ignores errors.
   */
  void shutdownAll(bool abortive=false);

 private:
  void doShutdown(int fd, bool abortive);

  // State transitions:
  // add():
  //   FREE -> IN_USE
  //
  // close():
  //   IN_USE -> (::close()) -> FREE
  //   SHUT_DOWN -> (::close()) -> FREE
  //   IN_SHUTDOWN -> MUST_CLOSE
  //   (If the socket is currently being shut down, we must defer the
  //    ::close() until the shutdown completes)
  //
  // shutdown():
  //   IN_USE -> IN_SHUTDOWN
  //   (::shutdown())
  //   IN_SHUTDOWN -> SHUT_DOWN
  //   MUST_CLOSE -> (::close()) -> FREE
  enum State : uint8_t {
    FREE = 0,
    IN_USE,
    IN_SHUTDOWN,
    SHUT_DOWN,
    MUST_CLOSE
  };

  struct Free {
    template <class T>
    void operator()(T* ptr) const {
      ::free(ptr);
    }
  };

  const size_t maxFd_;
  std::unique_ptr<std::atomic<uint8_t>[], Free> data_;
  folly::File nullFile_;
};

}  // namespaces
