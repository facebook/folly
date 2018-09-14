/*
 * Copyright 2017-present Facebook, Inc.
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

#include <folly/portability/PThread.h>

#if !FOLLY_HAVE_PTHREAD && _WIN32
#include <boost/thread/tss.hpp> // @manual

#include <errno.h>

#include <chrono>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>

#include <folly/lang/Assume.h>
#include <folly/portability/Windows.h>

namespace folly {
namespace portability {
namespace pthread {

int pthread_attr_init(pthread_attr_t* attr) {
  if (attr == nullptr) {
    errno = EINVAL;
    return -1;
  }
  attr->stackSize = 0;
  attr->detached = false;
  return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t* attr, int state) {
  if (attr == nullptr) {
    errno = EINVAL;
    return -1;
  }
  attr->detached = state == PTHREAD_CREATE_DETACHED ? true : false;
  return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attr, size_t kb) {
  if (attr == nullptr) {
    errno = EINVAL;
    return -1;
  }
  attr->stackSize = kb;
  return 0;
}

namespace pthread_detail {
pthread_t::~pthread_t() noexcept {
  if (handle != INVALID_HANDLE_VALUE && !detached) {
    CloseHandle(handle);
  }
}
} // namespace pthread_detail

int pthread_equal(pthread_t threadA, pthread_t threadB) {
  if (threadA == threadB) {
    return 1;
  }

  // Note that, in the presence of detached threads, it is in theory possible
  // for two different pthread_t handles to be compared as the same due to
  // Windows HANDLE and Thread ID re-use. If you're doing anything useful with
  // a detached thread, you're probably doing it wrong, but I felt like leaving
  // this note here anyways.
  if (threadA->handle == threadB->handle &&
      threadA->threadID == threadB->threadID) {
    return 1;
  }
  return 0;
}

namespace {
thread_local pthread_t current_thread_self;
struct pthread_startup_info {
  pthread_t thread;
  void* (*startupFunction)(void*);
  void* startupArgument;
};

DWORD internal_pthread_thread_start(void* arg) {
  // We are now in the new thread.
  auto startupInfo = reinterpret_cast<pthread_startup_info*>(arg);
  current_thread_self = startupInfo->thread;
  auto ret = startupInfo->startupFunction(startupInfo->startupArgument);
  if constexpr (sizeof(void*) != sizeof(DWORD)) {
    auto tmp = reinterpret_cast<uintptr_t>(ret);
    if (tmp > std::numeric_limits<DWORD>::max()) {
      throw std::out_of_range(
          "Exit code of the pthread is outside the range representable on Windows");
    }
  }

  delete startupInfo;
  return static_cast<DWORD>(reinterpret_cast<uintptr_t>(ret));
}
} // namespace

int pthread_create(
    pthread_t* thread,
    const pthread_attr_t* attr,
    void* (*start_routine)(void*),
    void* arg) {
  if (thread == nullptr) {
    errno = EINVAL;
    return -1;
  }

  size_t stackSize = attr != nullptr ? attr->stackSize : 0;
  bool detach = attr != nullptr ? attr->detached : false;

  // Note that the start routine passed into pthread returns a void* and the
  // windows API expects DWORD's, so we need to stub around that.
  auto startupInfo = new pthread_startup_info();
  startupInfo->startupFunction = start_routine;
  startupInfo->startupArgument = arg;
  startupInfo->thread = std::make_shared<pthread_detail::pthread_t>();
  // We create the thread suspended so we can assign the handle and thread id
  // in the pthread_t.
  startupInfo->thread->handle = CreateThread(
      nullptr,
      stackSize,
      internal_pthread_thread_start,
      startupInfo,
      CREATE_SUSPENDED,
      &startupInfo->thread->threadID);
  ResumeThread(startupInfo->thread->handle);

  if (detach) {
    *thread = std::make_shared<pthread_detail::pthread_t>();
    (*thread)->detached = true;
    (*thread)->handle = startupInfo->thread->handle;
    (*thread)->threadID = startupInfo->thread->threadID;
  } else {
    *thread = startupInfo->thread;
  }
  return 0;
}

pthread_t pthread_self() {
  // Not possible to race :)
  if (current_thread_self == nullptr) {
    current_thread_self = std::make_shared<pthread_detail::pthread_t>();
    current_thread_self->threadID = GetCurrentThreadId();
    // The handle returned by GetCurrentThread is a pseudo-handle and needs to
    // be swapped out for a real handle to be useful anywhere other than this
    // thread.
    DuplicateHandle(
        GetCurrentProcess(),
        GetCurrentThread(),
        GetCurrentProcess(),
        &current_thread_self->handle,
        DUPLICATE_SAME_ACCESS,
        TRUE,
        0);
  }

  return current_thread_self;
}

int pthread_join(pthread_t thread, void** exitCode) {
  if (thread->detached) {
    errno = EINVAL;
    return -1;
  }

  if (WaitForSingleObjectEx(thread->handle, INFINITE, FALSE) == WAIT_FAILED) {
    return -1;
  }

  if (exitCode != nullptr) {
    DWORD e;
    if (!GetExitCodeThread(thread->handle, &e)) {
      return -1;
    }
    *exitCode = reinterpret_cast<void*>(static_cast<uintptr_t>(e));
  }

  return 0;
}

HANDLE pthread_getw32threadhandle_np(pthread_t thread) {
  return thread->handle;
}

DWORD pthread_getw32threadid_np(pthread_t thread) {
  return thread->threadID;
}

int pthread_setschedparam(
    pthread_t thread,
    int policy,
    const sched_param* param) {
  if (thread->detached) {
    errno = EINVAL;
    return -1;
  }

  auto newPrior = param->sched_priority;
  if (newPrior > THREAD_PRIORITY_TIME_CRITICAL ||
      newPrior < THREAD_PRIORITY_IDLE) {
    errno = EINVAL;
    return -1;
  }
  if (GetPriorityClass(GetCurrentProcess()) != REALTIME_PRIORITY_CLASS) {
    if (newPrior > THREAD_PRIORITY_IDLE && newPrior < THREAD_PRIORITY_LOWEST) {
      // The values between IDLE and LOWEST are invalid unless the process is
      // running as realtime.
      newPrior = THREAD_PRIORITY_LOWEST;
    } else if (
        newPrior < THREAD_PRIORITY_TIME_CRITICAL &&
        newPrior > THREAD_PRIORITY_HIGHEST) {
      // Same as above.
      newPrior = THREAD_PRIORITY_HIGHEST;
    }
  }
  if (!SetThreadPriority(thread->handle, newPrior)) {
    return -1;
  }
  return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t* attr) {
  if (attr == nullptr) {
    return EINVAL;
  }

  attr->type = PTHREAD_MUTEX_DEFAULT;
  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr) {
  if (attr == nullptr) {
    return EINVAL;
  }

  return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type) {
  if (attr == nullptr) {
    return EINVAL;
  }

  if (type != PTHREAD_MUTEX_DEFAULT && type != PTHREAD_MUTEX_RECURSIVE) {
    return EINVAL;
  }

  attr->type = type;
  return 0;
}

struct pthread_mutex_t_ {
 private:
  int type;
  union {
    std::timed_mutex timed_mtx;
    std::recursive_timed_mutex recursive_timed_mtx;
  };

 public:
  pthread_mutex_t_(int mutex_type) : type(mutex_type) {
    switch (type) {
      case PTHREAD_MUTEX_DEFAULT:
        new (&timed_mtx) std::timed_mutex();
        break;
      case PTHREAD_MUTEX_RECURSIVE:
        new (&recursive_timed_mtx) std::recursive_timed_mutex();
        break;
    }
  }

  ~pthread_mutex_t_() noexcept {
    switch (type) {
      case PTHREAD_MUTEX_DEFAULT:
        timed_mtx.~timed_mutex();
        break;
      case PTHREAD_MUTEX_RECURSIVE:
        recursive_timed_mtx.~recursive_timed_mutex();
        break;
    }
  }

  void lock() {
    switch (type) {
      case PTHREAD_MUTEX_DEFAULT:
        timed_mtx.lock();
        break;
      case PTHREAD_MUTEX_RECURSIVE:
        recursive_timed_mtx.lock();
        break;
    }
  }

  bool try_lock() {
    switch (type) {
      case PTHREAD_MUTEX_DEFAULT:
        return timed_mtx.try_lock();
      case PTHREAD_MUTEX_RECURSIVE:
        return recursive_timed_mtx.try_lock();
    }
    folly::assume_unreachable();
  }

  bool timed_try_lock(std::chrono::system_clock::time_point until) {
    switch (type) {
      case PTHREAD_MUTEX_DEFAULT:
        return timed_mtx.try_lock_until(until);
      case PTHREAD_MUTEX_RECURSIVE:
        return recursive_timed_mtx.try_lock_until(until);
    }
    folly::assume_unreachable();
  }

  void unlock() {
    switch (type) {
      case PTHREAD_MUTEX_DEFAULT:
        timed_mtx.unlock();
        break;
      case PTHREAD_MUTEX_RECURSIVE:
        recursive_timed_mtx.unlock();
        break;
    }
  }
};

int pthread_mutex_init(
    pthread_mutex_t* mutex,
    const pthread_mutexattr_t* attr) {
  if (mutex == nullptr) {
    return EINVAL;
  }

  auto type = attr != nullptr ? attr->type : PTHREAD_MUTEX_DEFAULT;
  auto ret = new pthread_mutex_t_(type);
  *mutex = ret;
  return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
  if (mutex == nullptr) {
    return EINVAL;
  }

  delete *mutex;
  *mutex = nullptr;
  return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
  if (mutex == nullptr) {
    return EINVAL;
  }

  // This implementation does not implement deadlock detection, as the
  // STL mutexes we're wrapping don't either.
  (*mutex)->lock();
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
  if (mutex == nullptr) {
    return EINVAL;
  }

  if ((*mutex)->try_lock()) {
    return 0;
  } else {
    return EBUSY;
  }
}

int pthread_mutex_timedlock(
    pthread_mutex_t* mutex,
    const timespec* abs_timeout) {
  if (mutex == nullptr || abs_timeout == nullptr) {
    return EINVAL;
  }

  using time_point = std::chrono::system_clock::time_point;
  auto ns = std::chrono::seconds(abs_timeout->tv_sec) +
      std::chrono::nanoseconds(abs_timeout->tv_nsec);
  auto time = time_point(std::chrono::duration_cast<time_point::duration>(ns));

  if ((*mutex)->timed_try_lock(time)) {
    return 0;
  } else {
    return ETIMEDOUT;
  }
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
  if (mutex == nullptr) {
    return EINVAL;
  }

  // This implementation allows other threads to unlock it,
  // as the STL containers also do.
  (*mutex)->unlock();
  return 0;
}

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
  try {
    auto newKey = new boost::thread_specific_ptr<void>(destructor);
    *key = newKey;
    return 0;
  } catch (boost::thread_resource_error) {
    return -1;
  }
}

int pthread_key_delete(pthread_key_t key) {
  try {
    auto realKey = reinterpret_cast<boost::thread_specific_ptr<void>*>(key);
    delete realKey;
    return 0;
  } catch (boost::thread_resource_error) {
    return -1;
  }
}

void* pthread_getspecific(pthread_key_t key) {
  auto realKey = reinterpret_cast<boost::thread_specific_ptr<void>*>(key);
  // This can't throw as-per the documentation.
  return realKey->get();
}

int pthread_setspecific(pthread_key_t key, const void* value) {
  try {
    auto realKey = reinterpret_cast<boost::thread_specific_ptr<void>*>(key);
    // We can't just call reset here because that would invoke the cleanup
    // function, which we don't want to do.
    boost::detail::set_tss_data(
        realKey,
        boost::shared_ptr<boost::detail::tss_cleanup_function>(),
        const_cast<void*>(value),
        false);
    return 0;
  } catch (boost::thread_resource_error) {
    return -1;
  }
}
} // namespace pthread
} // namespace portability
} // namespace folly
#endif
