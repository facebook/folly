/*
 * Copyright 2014 Facebook, Inc.
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

#include "MemoryIdler.h"
#include <folly/Logging.h>
#include <folly/Malloc.h>
#include <folly/ScopeGuard.h>
#include <folly/detail/CacheLocality.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <utility>


// weak linking means the symbol will be null if not available, instead
// of a link failure
extern "C" int mallctl(const char *name, void *oldp, size_t *oldlenp,
                       void *newp, size_t newlen)
    __attribute__((weak));


namespace folly { namespace detail {

AtomicStruct<std::chrono::steady_clock::duration>
MemoryIdler::defaultIdleTimeout(std::chrono::seconds(5));


/// Calls mallctl, optionally reading and/or writing an unsigned value
/// if in and/or out is non-null.  Logs on error
static unsigned mallctlWrapper(const char* cmd, const unsigned* in,
                               unsigned* out) {
  size_t outLen = sizeof(unsigned);
  int err = mallctl(cmd,
                    out, out ? &outLen : nullptr,
                    const_cast<unsigned*>(in), in ? sizeof(unsigned) : 0);
  if (err != 0) {
    FB_LOG_EVERY_MS(WARNING, 10000)
      << "mallctl " << cmd << ": " << strerror(err) << " (" << err << ")";
  }
  return err;
}

void MemoryIdler::flushLocalMallocCaches() {
  if (usingJEMalloc()) {
    if (!mallctl) {
      FB_LOG_EVERY_MS(ERROR, 10000) << "mallctl weak link failed";
      return;
    }

    // "tcache.flush" was renamed to "thread.tcache.flush" in jemalloc 3
    (void)mallctlWrapper("thread.tcache.flush", nullptr, nullptr);

    // By default jemalloc has 4 arenas per cpu, and then assigns each
    // thread to one of those arenas.  This means that in any service
    // that doesn't perform a lot of context switching, the chances that
    // another thread will be using the current thread's arena (and hence
    // doing the appropriate dirty-page purging) are low.  Some good
    // tuned configurations (such as that used by hhvm) use fewer arenas
    // and then pin threads to avoid contended access.  In that case,
    // purging the arenas is counter-productive.  We use the heuristic
    // that if narenas <= 2 * num_cpus then we shouldn't do anything here,
    // which detects when the narenas has been reduced from the default
    unsigned narenas;
    unsigned arenaForCurrent;
    if (mallctlWrapper("arenas.narenas", nullptr, &narenas) == 0 &&
        narenas > 2 * CacheLocality::system().numCpus &&
        mallctlWrapper("thread.arena", nullptr, &arenaForCurrent) == 0) {
      (void)mallctlWrapper("arenas.purge", &arenaForCurrent, nullptr);
    }
  }
}


#ifdef __x86_64__

static const size_t s_pageSize = sysconf(_SC_PAGESIZE);
static __thread uintptr_t tls_stackLimit;
static __thread size_t tls_stackSize;

static void fetchStackLimits() {
  pthread_attr_t attr;
#if defined(_GNU_SOURCE) && defined(__linux__) // Linux+GNU extension
  pthread_getattr_np(pthread_self(), &attr);
#else
  pthread_attr_init(&attr);
#endif
  SCOPE_EXIT { pthread_attr_destroy(&attr); };

  void* addr;
  size_t rawSize;
  int err;
  if ((err = pthread_attr_getstack(&attr, &addr, &rawSize))) {
    // unexpected, but it is better to continue in prod than do nothing
    FB_LOG_EVERY_MS(ERROR, 10000) << "pthread_attr_getstack error " << err;
    assert(false);
    tls_stackSize = 1;
    return;
  }
  assert(addr != nullptr);
  assert(rawSize >= PTHREAD_STACK_MIN);

  // glibc subtracts guard page from stack size, even though pthread docs
  // seem to imply the opposite
  size_t guardSize;
  if (pthread_attr_getguardsize(&attr, &guardSize) != 0) {
    guardSize = 0;
  }
  assert(rawSize > guardSize);

  // stack goes down, so guard page adds to the base addr
  tls_stackLimit = uintptr_t(addr) + guardSize;
  tls_stackSize = rawSize - guardSize;

  assert((tls_stackLimit & (s_pageSize - 1)) == 0);
}

static __attribute__((noinline)) uintptr_t getStackPtr() {
  char marker;
  auto rv = uintptr_t(&marker);
  return rv;
}

void MemoryIdler::unmapUnusedStack(size_t retain) {
  if (tls_stackSize == 0) {
    fetchStackLimits();
  }
  if (tls_stackSize <= std::max(size_t(1), retain)) {
    // covers both missing stack info, and impossibly large retain
    return;
  }

  auto sp = getStackPtr();
  assert(sp >= tls_stackLimit);
  assert(sp - tls_stackLimit < tls_stackSize);

  auto end = (sp - retain) & ~(s_pageSize - 1);
  if (end <= tls_stackLimit) {
    // no pages are eligible for unmapping
    return;
  }

  size_t len = end - tls_stackLimit;
  assert((len & (s_pageSize - 1)) == 0);
  if (madvise((void*)tls_stackLimit, len, MADV_DONTNEED) != 0) {
    // It is likely that the stack vma hasn't been fully grown.  In this
    // case madvise will apply dontneed to the present vmas, then return
    // errno of ENOMEM.  We can also get an EAGAIN, theoretically.
    // EINVAL means either an invalid alignment or length, or that some
    // of the pages are locked or shared.  Neither should occur.
    int e = errno;
    assert(e == EAGAIN || e == ENOMEM);
  }
}

#else

void MemoryIdler::unmapUnusedStack(size_t retain) {
}

#endif

}}
