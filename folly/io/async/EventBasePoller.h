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

#include <chrono>
#include <memory>

#include <folly/small_vector.h>

namespace folly::detail {

/**
 * EventBasePoller multiplexes the pollable fds of multiple EventBases, so that
 * a pool of threads can drive them without one thread per EventBase.
 *
 * wait() blocks until some fds are ready and returns their handles. A ready
 * EventBase can be driven until it would block; then handoff() must be called
 * to resume polling it.
 *
 * At most one thread can be in wait() at any time; consecutive calls may come
 * from different threads if ordered by happens-before. Other methods are
 * thread-safe. With io_uring, a thread that has called wait() must not exit
 * while handles are registered, as its in-flight polls would be cancelled.
 */
class EventBasePoller {
 public:
  class Handle {
   public:
    virtual ~Handle();

    template <class T>
    T* getUserData() const {
      return reinterpret_cast<T*>(userData_);
    }

    // Re-arms the fd for polling (done=false) or marks the handle as finished
    // so it can be reclaimed (done=true).
    virtual void handoff(bool done) = 0;

   protected:
    explicit Handle(void* userData) : userData_(userData) {}

    void* userData_;
  };

  // epoll with inline rearm is the simplest configuration and the preferred
  // one; the other backends and modes exist for experimentation.
  struct Options {
    enum class Backend { kEpoll, kIoUring };

    // Must be user-provided for create()'s default argument to compile.
    Options() {}

    Backend backend{Backend::kEpoll};
    bool epollRearmInline{true};
    std::chrono::microseconds spinTimeout{10};
    std::chrono::microseconds sleepBeforeBlock{0};
    size_t epollMaxEvents{64};
    size_t ioUringSqEntries{128};
  };

  static std::unique_ptr<EventBasePoller> create(Options options = {});

  virtual ~EventBasePoller();

  virtual std::unique_ptr<Handle> add(int fd, void* userData) = 0;

  // Blocks until handoff(true) is called on the handle.
  virtual void reclaim(std::unique_ptr<Handle> handle) = 0;

  // Blocks until at least one handle is ready. Returns ready handles.
  // Returns empty only after shutdown().
  virtual small_vector<Handle*, 4> wait() = 0;

  // Makes a blocking wait() call return, empty unless some handles were ready.
  // Subsequent wait() calls return empty.
  virtual void shutdown() = 0;
};

} // namespace folly::detail
