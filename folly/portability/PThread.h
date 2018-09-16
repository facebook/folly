/*
 * Copyright 2016-present Facebook, Inc.
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

#include <folly/portability/Config.h>

#if !defined(_WIN32)

#include <pthread.h> // nolint

#elif !FOLLY_HAVE_PTHREAD

#include <cstdint>
#include <memory>

#include <folly/portability/Sched.h>
#include <folly/portability/Time.h>
#include <folly/portability/Windows.h> // nolint

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL

#define _POSIX_TIMEOUTS 200112L

namespace folly {
namespace portability {
namespace pthread {
struct pthread_attr_t {
  size_t stackSize;
  bool detached;
};

int pthread_attr_init(pthread_attr_t* attr);
int pthread_attr_setdetachstate(pthread_attr_t* attr, int state);
int pthread_attr_setstacksize(pthread_attr_t* attr, size_t kb);

namespace pthread_detail {
struct pthread_t {
  HANDLE handle{INVALID_HANDLE_VALUE};
  DWORD threadID{0};
  bool detached{false};

  ~pthread_t() noexcept;
};
} // namespace pthread_detail
using pthread_t = std::shared_ptr<pthread_detail::pthread_t>;

int pthread_equal(pthread_t threadA, pthread_t threadB);
int pthread_create(
    pthread_t* thread,
    const pthread_attr_t* attr,
    void* (*start_routine)(void*),
    void* arg);
pthread_t pthread_self();
int pthread_join(pthread_t thread, void** exitCode);

HANDLE pthread_getw32threadhandle_np(pthread_t thread);
DWORD pthread_getw32threadid_np(pthread_t thread);

int pthread_setschedparam(
    pthread_t thread,
    int policy,
    const sched_param* param);

struct pthread_mutexattr_t {
  int type;
};
int pthread_mutexattr_init(pthread_mutexattr_t* attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t* attr);
int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type);

using pthread_mutex_t = struct pthread_mutex_t_*;
int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_trylock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);
int pthread_mutex_timedlock(
    pthread_mutex_t* mutex,
    const timespec* abs_timeout);

using pthread_rwlock_t = struct pthread_rwlock_t_*;
// Technically the second argument here is supposed to be a
// const pthread_rwlockattr_t* but we don support it, so we
// simply don't define pthread_rwlockattr_t at all to cause
// a build-break if anyone tries to use it.
int pthread_rwlock_init(pthread_rwlock_t* rwlock, const void* attr);
int pthread_rwlock_destroy(pthread_rwlock_t* rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_timedrdlock(
    pthread_rwlock_t* rwlock,
    const timespec* abs_timeout);
int pthread_rwlock_wrlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_trywrlock(pthread_rwlock_t* rwlock);
int pthread_rwlock_timedwrlock(
    pthread_rwlock_t* rwlock,
    const timespec* abs_timeout);
int pthread_rwlock_unlock(pthread_rwlock_t* rwlock);

using pthread_cond_t = struct pthread_cond_t_*;
// Once again, technically the second argument should be a
// pthread_condattr_t, but we don't implement it, so void*
// it is.
int pthread_cond_init(pthread_cond_t* cond, const void* attr);
int pthread_cond_destroy(pthread_cond_t* cond);
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
int pthread_cond_timedwait(
    pthread_cond_t* cond,
    pthread_mutex_t* mutex,
    const timespec* abstime);
int pthread_cond_signal(pthread_cond_t* cond);
int pthread_cond_broadcast(pthread_cond_t* cond);

// In reality, this is boost::thread_specific_ptr*, but we're attempting
// to avoid introducing boost into a portability header.
using pthread_key_t = void*;

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
void* pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void* value);
} // namespace pthread
} // namespace portability
} // namespace folly

/* using override */ using namespace folly::portability::pthread;

#else
// The pthread implementation we support on Windows is a bit of a pain to work
// with in certain places. This entire mess here exists for exactly one reason:
// `using pid_t = int;`
// Without all of this mess, the pthread implementation will attempt to define
// `pid_t` as `void*` which is incompatible with just about every other
// definition of `pid_t` used by other libraries defining it on Windows, for
// example, python.

// On Windows, define mode_t and pid_t
#include <folly/portability/SysTypes.h>

// HANDLE and DWORD for the declarations further down.
#include <folly/portability/Windows.h> // nolint

// Because of INCLUDE_NP, `errno.h` doesn't get included by pthread, causing it
// to define the errno values itself, which causes conflicts when `errno.h` ends
// up actually getting included, so we include it explicitly here to prevent it
// from defining the values itself.
#include <errno.h>

// Pretend we are building with AT&T's UWIN project, which is a Unix API for
// Windows 95 & Windows NT. Yes, really. https://github.com/att/uwin
// This is the core define that keeps `pthread.h` from defining `pid_t`.
#define _UWIN 1

// Because we've defined `_UWIN`, the pthread implementation thinks that the
// pthread types have all also already been defined by default. By defining
// this, we force `PTW32_LEVEL` to be defined as `2`, which is enough to get it
// to define the pthread types for us.
#define INCLUDE_NP 1

// By defining `_UWIN` we cause the pthread implementation to aggressively
// define `HAVE_MODE_T`, which we define in `folly/portability/SysTypes.h` to
// keep it from defining an incompatible version of it. We undefine the macro
// here to keep from generating warnings when the implementation defines it.
// Note that the implementation leaks its definition of `HAVE_MODE_T`, so we
// don't need to re-define it after.
#undef HAVE_MODE_T

#include <pthread.h> // nolint

#ifndef HAVE_MODE_T
#error We expected pthread.h to define HAVE_MODE_T but that did not happen.
#endif

// Now clean up our mess so nothing else thinks we're doing crazy things.
#undef _UWIN
#undef INCLUDE_NP

// Because we defined `INCLUDE_NP` above, the non-portable APIs don't actually
// get declared. We still need them, so declare them ourselves instead.
extern "C" {
PTW32_DLLPORT HANDLE PTW32_CDECL
pthread_getw32threadhandle_np(pthread_t thread);
PTW32_DLLPORT DWORD PTW32_CDECL pthread_getw32threadid_np(pthread_t thread);
}

// And now everything else that isn't just here for `pid_t`.

// We implement a sane comparison operand for
// pthread_t and an integer so that it may be
// compared against 0.
inline bool operator==(pthread_t ptA, unsigned int b) {
  if (ptA.p == nullptr) {
    return b == 0;
  }
  return pthread_getw32threadid_np(ptA) == b;
}

inline bool operator!=(pthread_t ptA, unsigned int b) {
  if (ptA.p == nullptr) {
    return b != 0;
  }
  return pthread_getw32threadid_np(ptA) != b;
}

inline bool operator==(pthread_t ptA, pthread_t ptB) {
  return pthread_equal(ptA, ptB) != 0;
}

inline bool operator!=(pthread_t ptA, pthread_t ptB) {
  return pthread_equal(ptA, ptB) == 0;
}

inline bool operator<(pthread_t ptA, pthread_t ptB) {
  return ptA.p < ptB.p;
}

inline bool operator!(pthread_t ptA) {
  return ptA == 0;
}

inline int pthread_attr_getstack(
    pthread_attr_t* attr,
    void** stackaddr,
    size_t* stacksize) {
  if (pthread_attr_getstackaddr(attr, stackaddr) != 0) {
    return -1;
  }
  if (pthread_attr_getstacksize(attr, stacksize) != 0) {
    return -1;
  }
  return 0;
}

inline int
pthread_attr_setstack(pthread_attr_t* attr, void* stackaddr, size_t stacksize) {
  if (pthread_attr_setstackaddr(attr, stackaddr) != 0) {
    return -1;
  }
  if (pthread_attr_setstacksize(attr, stacksize) != 0) {
    return -1;
  }
  return 0;
}

inline int pthread_attr_getguardsize(
    pthread_attr_t* /* attr */,
    size_t* guardsize) {
  *guardsize = 0;
  return 0;
}

#include <functional>
namespace std {
template <>
struct hash<pthread_t> {
  std::size_t operator()(const pthread_t& k) const {
    return 0 ^ std::hash<decltype(k.p)>()(k.p) ^
        std::hash<decltype(k.x)>()(k.x);
  }
};
} // namespace std
#endif
