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

#pragma once

#include <cerrno>
#include <limits>

#include <folly/Utility.h>
#include <folly/lang/Exception.h>
#include <folly/portability/Windows.h>

#if defined(_WIN32)

#elif defined(__APPLE__)

#include <dispatch/dispatch.h> // @manual

#else

#include <semaphore.h>

#endif

namespace folly {

#if defined(_WIN32)

class NativeSemaphore {
 public:
  static constexpr size_t value_max_v =
      static_cast<size_t>(std::numeric_limits<int>::max());

  NativeSemaphore() : NativeSemaphore(0) {}
  explicit NativeSemaphore(size_t value) {
    if (value > value_max_v) {
      throw_exception<std::runtime_error>("sem: value too large");
    }
    auto sem = CreateSemaphoreA(nullptr, value, value_max_v, nullptr);
    if (!sem) {
      throw_exception<std::runtime_error>("sem: init failed");
    }
    sem_ = sem;
  }

  ~NativeSemaphore() {
    if (!CloseHandle(sem_)) {
      terminate_with<std::runtime_error>("sem: fini failed");
    }
    sem_ = INVALID_HANDLE_VALUE;
  }

  void post() {
    if (!ReleaseSemaphore(sem_, 1, nullptr)) {
      throw_exception<std::runtime_error>("sem: post failed");
    }
  }

  void wait() {
    switch (WaitForSingleObject(sem_, INFINITE)) {
      case WAIT_OBJECT_0:
        return;
      default:
        throw_exception<std::runtime_error>("sem: wait failed");
    }
  }

  bool try_wait() {
    switch (WaitForSingleObject(sem_, 0)) {
      case WAIT_OBJECT_0:
        return true;
      case WAIT_TIMEOUT:
        return false;
      default:
        throw_exception<std::runtime_error>("sem: wait failed");
    }
  }

 private:
  HANDLE sem_{INVALID_HANDLE_VALUE};
};

#elif defined(__APPLE__)

class NativeSemaphore {
 public:
  static constexpr size_t value_max_v =
      static_cast<size_t>(std::numeric_limits<intptr_t>::max());

  NativeSemaphore() : NativeSemaphore(0) {}
  explicit NativeSemaphore(size_t value) {
    if (value > value_max_v) {
      throw_exception<std::runtime_error>("sem: value too large");
    }
    sem_ = dispatch_semaphore_create(to_signed(value));
  }

  ~NativeSemaphore() {
    dispatch_release(sem_);
    sem_ = {};
  }

  void post() { dispatch_semaphore_signal(sem_); }

  void wait() { dispatch_semaphore_wait(sem_, DISPATCH_TIME_FOREVER); }

  bool try_wait() { return !dispatch_semaphore_wait(sem_, 0); }

 private:
  dispatch_semaphore_t sem_{};
};

#else

class NativeSemaphore {
 public:
  static constexpr size_t value_max_v =
      static_cast<size_t>(std::numeric_limits<int>::max());

  NativeSemaphore() : NativeSemaphore(0) {}
  explicit NativeSemaphore(size_t value) {
    if (value > value_max_v) {
      throw_exception<std::runtime_error>("sem: value too large");
    }
    if (sem_init(&sem_, 0, to_narrow(value))) {
      throw_exception<std::runtime_error>("sem: init failed");
    }
  }

  ~NativeSemaphore() {
    if (sem_destroy(&sem_)) {
      terminate_with<std::runtime_error>("sem: fini failed");
    }
    sem_ = {};
  }

  void post() {
    if (sem_post(&sem_)) {
      throw_exception<std::runtime_error>("sem: post failed");
    }
  }

  void wait() {
  start:
    if (sem_wait(&sem_)) {
      switch (errno) {
        case EINTR:
          goto start;
        default:
          throw_exception<std::runtime_error>("sem: wait failed");
      }
    }
  }

  bool try_wait() {
    if (sem_trywait(&sem_)) {
      switch (errno) {
        case EAGAIN:
          return false;
        default:
          throw_exception<std::runtime_error>("sem: wait failed");
      }
    }
    return true;
  }

 private:
  sem_t sem_{};
};

#endif

} // namespace folly
