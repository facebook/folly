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

#include <folly/fibers/GuardPageAllocator.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <mutex>

#include <glog/logging.h>

#include <folly/Singleton.h>
#include <folly/SpinLock.h>
#include <folly/Synchronized.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/Unistd.h>

// The fiber stack-overflow handler dumps the offending fiber's stack before
// delegating to the previous SIGSEGV action (which typically aborts): the
// resulting coredump otherwise captures only the abort path, not the recursion
// that overflowed the fiber stack, leaving the real culprit unrecoverable.
//
// On server builds we symbolize inline, matching folly's other fatal-signal
// output. On mobile builds we capture raw frame addresses only (symbolized
// offline, as mobile crash addresses already are): pulling the symbolizer into
// this ubiquitous library perturbs Android native-lib merging and duplicates
// the folly Timekeeper singleton across merged .so's, aborting at app startup
// via singletonWarnDoubleRegistrationAndAbort (see the revert of D113295814).
// FOLLY_FIBERS_SYMBOLIZE_OVERFLOW is defined only by the server (fbcode) build,
// which is also the only build that links the full symbolizer here.
#if FOLLY_FIBERS_SYMBOLIZE_OVERFLOW
#include <folly/debugging/symbolizer/Symbolizer.h>
#include <folly/synchronization/CallOnce.h>
#else
#include <folly/debugging/symbolizer/StackTrace.h>
#endif

namespace folly {
namespace fibers {

/**
 * Each stack with a guard page creates two memory mappings.
 * Since this is a limited resource, we don't want to create too many of these.
 *
 * The upper bound on total number of mappings created
 * is kNumGuarded * kMaxInUse.
 */

/**
 * Number of guarded stacks per allocator instance
 */
constexpr size_t kNumGuarded = 100;

/**
 * Maximum number of allocator instances with guarded stacks enabled
 */
constexpr size_t kMaxInUse = 100;

/**
 * A cache for kNumGuarded stacks of a given size
 *
 * Thread safe.
 */
class StackCache {
 public:
  explicit StackCache(size_t stackSize, size_t guardPagesPerStack)
      : allocSize_(allocSize(stackSize, guardPagesPerStack)),
        guardPagesPerStack_(guardPagesPerStack) {
    auto p = ::mmap(
        nullptr,
        allocSize_ * kNumGuarded,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    PCHECK(p != (void*)(-1));
    storage_ = reinterpret_cast<unsigned char*>(p);

    /* Protect the bottommost page of every stack allocation */
    freeList_.reserve(kNumGuarded);
    for (size_t i = 0; i < kNumGuarded; ++i) {
      auto allocBegin = storage_ + allocSize_ * i;
      freeList_.emplace_back(allocBegin, /* protected= */ false);
    }
  }

  unsigned char* borrow(size_t size) {
    std::lock_guard lg(lock_);

    assert(storage_);

    auto as = allocSize(size, guardPagesPerStack_);
    if (as != allocSize_ || freeList_.empty()) {
      return nullptr;
    }

    auto p = freeList_.back().first;
    if (!freeList_.back().second) {
      PCHECK(0 == ::mprotect(p, pagesize() * guardPagesPerStack_, PROT_NONE));
      protectedRanges().wlock()->insert(
          std::make_pair(
              reinterpret_cast<intptr_t>(p),
              reinterpret_cast<intptr_t>(
                  p + pagesize() * guardPagesPerStack_)));
    }
    freeList_.pop_back();

    /* We allocate minimum number of pages required, plus a guard page.
       Since we use this for stack storage, requested allocation is aligned
       at the top of the allocated pages, while the guard page is at the bottom.

               -- increasing addresses -->
             Guard page     Normal pages
            |xxxxxxxxxx|..........|..........|
            <- allocSize_ ------------------->
         p -^                <- size -------->
                      limit -^
    */
    auto limit = p + allocSize_ - size;
    assert(limit >= p + pagesize() * guardPagesPerStack_);
    return limit;
  }

  bool giveBack(unsigned char* limit, size_t size) {
    std::lock_guard lg(lock_);

    assert(storage_);

    auto as = allocSize(size, guardPagesPerStack_);
    if (std::less_equal<void*>{}(limit, storage_) ||
        std::less_equal<void*>{}(storage_ + allocSize_ * kNumGuarded, limit)) {
      /* not mine */
      return false;
    }

    auto p = limit + size - as;
    assert(as == allocSize_);
    assert((p - storage_) % allocSize_ == 0);
    freeList_.emplace_back(p, /* protected= */ true);
    return true;
  }

  ~StackCache() {
    assert(storage_);
    protectedRanges().withWLock([&](auto& ranges) {
      for (const auto& item : freeList_) {
        ranges.erase(
            std::make_pair(
                reinterpret_cast<intptr_t>(item.first),
                reinterpret_cast<intptr_t>(
                    item.first + pagesize() * guardPagesPerStack_)));
      }
    });
    PCHECK(0 == ::munmap(storage_, allocSize_ * kNumGuarded));
  }

  static bool isProtected(intptr_t addr) {
    // Use a read lock for reading.
    return protectedRanges().withRLock([&](auto const& ranges) {
      for (const auto& range : ranges) {
        if (range.first <= addr && addr < range.second) {
          return true;
        }
      }
      return false;
    });
  }

 private:
  folly::SpinLock lock_;
  unsigned char* storage_{nullptr};
  const size_t allocSize_{0};
  const size_t guardPagesPerStack_{0};

  /**
   * LIFO free list. Each pair contains stack pointer and protected flag.
   */
  std::vector<std::pair<unsigned char*, bool>> freeList_;

  static size_t pagesize() {
    static const auto pagesize = size_t(sysconf(_SC_PAGESIZE));
    return pagesize;
  }

  /**
   * Returns a multiple of pagesize() enough to store size + a few guard pages
   */
  static size_t allocSize(size_t size, size_t guardPages) {
    return pagesize() * ((size + pagesize() * guardPages - 1) / pagesize() + 1);
  }

  /**
   * For each [b, e) range in this set, the bytes in the range were mprotected.
   */
  static folly::Synchronized<std::unordered_set<std::pair<intptr_t, intptr_t>>>&
  protectedRanges() {
    static auto instance = new folly::Synchronized<
        std::unordered_set<std::pair<intptr_t, intptr_t>>>();
    return *instance;
  }
};

#ifndef _WIN32

namespace {

struct sigaction oldSigsegvAction;

#if FOLLY_FIBERS_SYMBOLIZE_OVERFLOW
// Pre-allocated in installGuardPageSignalHandler() (the constructors allocate,
// which is not async-signal-safe). Two printers are kept because the right one
// depends on the faulting thread's alternate-stack size, which is not known
// when the process-wide handler is installed: fibers configure a per-thread
// SA_ONSTACK alternate stack, defaulting to 32KB, and SafeStackTracePrinter
// symbolization can overflow one that small.
// NOLINTNEXTLINE(facebook-avoid-non-const-global-variables)
folly::symbolizer::SafeStackTracePrinter* gFiberOverflowStackPrinter = nullptr;
#if FOLLY_HAVE_SWAPCONTEXT
folly::symbolizer::UnsafeSelfAllocateStackTracePrinter*
    // NOLINTNEXTLINE(facebook-avoid-non-const-global-variables)
    gFiberOverflowUnsafeStackPrinter = nullptr;

// Mirrors folly::symbolizer's small-sigaltstack threshold:
// SafeStackTracePrinter symbolization can overflow an alternate signal stack at
// or below this size.
constexpr size_t kSmallSigAltStackSize = 64 * 1024;

// Whether this thread's SA_ONSTACK alternate stack is small enough that
// SafeStackTracePrinter symbolization risks overflowing it. Signal-safe: it
// only reads per-thread kernel state, with no allocation or locking.
bool onSmallSigAltStack() {
#if FOLLY_APPLE_TVOS || FOLLY_APPLE_WATCHOS
  // sigaltstack is banned on tvOS and watchOS.
  return false;
#else
  stack_t ss;
  if (sigaltstack(nullptr, &ss) != 0) {
    return false;
  }
  if ((ss.ss_flags & SS_DISABLE) != 0) {
    return false;
  }
  return ss.ss_size <= kSmallSigAltStackSize;
#endif
}
#endif // FOLLY_HAVE_SWAPCONTEXT

// Symbolize and print the offending fiber's stack. Only the first
// concurrently-overflowing fiber prints; the rest skip straight to delegating
// rather than racing the shared printer's buffers. No release: we abort next.
void dumpFiberOverflowStack() {
  if (!gFiberOverflowStackPrinter) {
    return;
  }
  static std::atomic<bool> dumping{false};
  if (dumping.exchange(true)) {
    return;
  }
  folly::symbolizer::SafeStackTracePrinter* printer =
      gFiberOverflowStackPrinter;
#if FOLLY_HAVE_SWAPCONTEXT
  if (gFiberOverflowUnsafeStackPrinter && onSmallSigAltStack()) {
    printer = gFiberOverflowUnsafeStackPrinter;
  }
#endif
  printer->printStackTrace(/* symbolize */ true);
}
#else // !FOLLY_FIBERS_SYMBOLIZE_OVERFLOW
// Capture-only path. getStackTraceSafe() is the same async-signal-safe
// libunwind capture SafeStackTracePrinter uses internally, from the lightweight
// :stack_trace target; we write raw frame addresses and symbolize offline.
void writeStderrRaw(const char* buf, size_t len) {
  while (len > 0) {
    const ssize_t written = ::write(STDERR_FILENO, buf, len);
    if (written < 0) {
      // Retry a signal-interrupted write so the diagnostic isn't truncated;
      // bail on any other error rather than spin.
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (written == 0) {
      break;
    }
    buf += written;
    len -= static_cast<size_t>(written);
  }
}

void dumpFiberOverflowStack() {
  static std::atomic<bool> dumping{false};
  if (dumping.exchange(true)) {
    return;
  }
  // Static, not a local: this runs on the small per-fiber sigaltstack. Safe
  // only because `dumping` above is never cleared, so this is single-entry for
  // the life of the process -- if that ever changes, this must go back on the
  // stack.
  constexpr size_t kMaxAddresses = 100;
  static uintptr_t addresses[kMaxAddresses];
  const ssize_t n =
      folly::symbolizer::getStackTraceSafe(addresses, kMaxAddresses);
  if (n <= 0) {
    return;
  }
  static constexpr char kHeader[] =
      "folly::fibers offending fiber stack (raw addresses, symbolize offline):\n";
  writeStderrRaw(kHeader, sizeof(kHeader) - 1);
  constexpr size_t kNibbles = 2 * sizeof(uintptr_t);
  for (ssize_t i = 0; i < n; ++i) {
    char line[2 + kNibbles + 1];
    char* p = line;
    *p++ = '0';
    *p++ = 'x';
    const uintptr_t v = addresses[i];
    for (size_t shift = kNibbles; shift-- > 0;) {
      const unsigned nibble = (v >> (shift * 4)) & 0xFU;
      *p++ =
          static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
    }
    *p++ = '\n';
    writeStderrRaw(line, static_cast<size_t>(p - line));
  }
}
#endif // FOLLY_FIBERS_SYMBOLIZE_OVERFLOW

FOLLY_NOINLINE void FOLLY_FIBERS_STACK_OVERFLOW_DETECTED(
    int signum, siginfo_t* info, void* ucontext) {
  std::cerr << "folly::fibers Fiber stack overflow detected." << std::endl;
  // Dump the offending fiber's stack before delegating. We run on the
  // SA_ONSTACK alternate stack, so unwinding through the signal frame into the
  // exhausted fiber context is safe.
  dumpFiberOverflowStack();
  // Let the old signal handler handle the signal, but make this function name
  // present in the stack trace.
  if (oldSigsegvAction.sa_flags & SA_SIGINFO) {
    oldSigsegvAction.sa_sigaction(signum, info, ucontext);
  } else {
    oldSigsegvAction.sa_handler(signum);
  }
  // Prevent tail call optimization.
  std::cerr << "";
}

void sigsegvSignalHandler(int signum, siginfo_t* info, void* ucontext) {
  // Restore old signal handler
  sigaction(signum, &oldSigsegvAction, nullptr);

  if (signum != SIGSEGV) {
    std::cerr << "GuardPageAllocator signal handler called for signal: "
              << signum;
    return;
  }

  if (info &&
      StackCache::isProtected(reinterpret_cast<intptr_t>(info->si_addr))) {
    FOLLY_FIBERS_STACK_OVERFLOW_DETECTED(signum, info, ucontext);
    return;
  }

  // Let the old signal handler handle the signal. Invoke this synchronously
  // within our own signal handler to ensure that the kernel siginfo context
  // is not lost.
  if (oldSigsegvAction.sa_flags & SA_SIGINFO) {
    oldSigsegvAction.sa_sigaction(signum, info, ucontext);
  } else {
    oldSigsegvAction.sa_handler(signum);
  }
}

bool isInJVM() {
  auto getCreated = dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
  return getCreated;
}

void maybeInstallGuardPageSignalHandler() {
  static std::once_flag onceFlag;
  std::call_once(onceFlag, installGuardPageSignalHandler);
}
} // namespace

void installGuardPageSignalHandler() {
  if (isInJVM()) {
    // Don't install signal handler, since JVM internal signal handler doesn't
    // work with SA_ONSTACK
    return;
  }

#if FOLLY_FIBERS_SYMBOLIZE_OVERFLOW
  // Allocate the stack-trace printer(s) up front and exactly once: their
  // constructors allocate, which is not async-signal-safe, so it must not
  // happen in the handler. This public API may be called again to re-install
  // the handler after other code overrides SIGSEGV; the handler reads these
  // pointers without synchronization from other threads, so publish them once
  // via call_once rather than overwriting (which would race a concurrent
  // handler and leak the previously allocated printers).
  static folly::once_flag printerOnceFlag;
  folly::call_once(printerOnceFlag, [] {
    gFiberOverflowStackPrinter = new folly::symbolizer::SafeStackTracePrinter();
#if FOLLY_HAVE_SWAPCONTEXT
    gFiberOverflowUnsafeStackPrinter =
        new folly::symbolizer::UnsafeSelfAllocateStackTracePrinter();
#endif
  });
#endif // FOLLY_FIBERS_SYMBOLIZE_OVERFLOW

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
#if FOLLY_FIBERS_SYMBOLIZE_OVERFLOW && FOLLY_HAVE_SWAPCONTEXT
  // On a small alternate stack the handler symbolizes with the self-allocating
  // printer, which is not async-signal-safe. Block all signals while the
  // handler runs so a nested signal can't re-enter it and lose the original
  // overflow diagnostic. Mirrors folly's fatal signal handler.
  sigfillset(&sa.sa_mask);
#else
  sigemptyset(&sa.sa_mask);
#endif
  // By default signal handlers are run on the signaled thread's stack.
  // In case of stack overflow running the SIGSEGV signal handler on
  // the same stack leads to another SIGSEGV and crashes the program.
  // Use SA_ONSTACK, so alternate stack is used (only if configured via
  // sigaltstack).
  sa.sa_flags |= SA_SIGINFO | SA_ONSTACK;
  sa.sa_sigaction = &sigsegvSignalHandler;
  sigaction(SIGSEGV, &sa, &oldSigsegvAction);
}

#endif // _WIN32

/*
 * RAII Wrapper around a StackCache that calls
 * CacheManager::giveBack() on destruction.
 */
class StackCacheEntry {
 public:
  explicit StackCacheEntry(size_t stackSize, size_t guardPagesPerStack)
      : stackCache_(
            std::make_unique<StackCache>(stackSize, guardPagesPerStack)) {}

  StackCache& cache() const noexcept { return *stackCache_; }

  ~StackCacheEntry();

 private:
  std::unique_ptr<StackCache> stackCache_;
};

class CacheManager {
 public:
  static CacheManager& instance() {
    static auto inst = new CacheManager();
    return *inst;
  }

  std::unique_ptr<StackCacheEntry> getStackCache(
      size_t stackSize, size_t guardPagesPerStack) {
    auto used = inUse_.load(std::memory_order_relaxed);
    do {
      if (used >= kMaxInUse) {
        return nullptr;
      }
    } while (!inUse_.compare_exchange_weak(
        used, used + 1, std::memory_order_acquire, std::memory_order_relaxed));
    return std::make_unique<StackCacheEntry>(stackSize, guardPagesPerStack);
  }

 private:
  std::atomic<size_t> inUse_{0};

  friend class StackCacheEntry;

  void giveBack(std::unique_ptr<StackCache> /* stackCache_ */) {
    [[maybe_unused]] auto wasUsed =
        inUse_.fetch_sub(1, std::memory_order_release);
    assert(wasUsed > 0);
    /* Note: we can add a free list for each size bucket
       if stack re-use is important.
       In this case this needs to be a folly::Singleton
       to make sure the free list is cleaned up on fork.

       TODO(t7351705): fix Singleton destruction order
    */
  }
};

StackCacheEntry::~StackCacheEntry() {
  CacheManager::instance().giveBack(std::move(stackCache_));
}

GuardPageAllocator::GuardPageAllocator(size_t guardPagesPerStack)
    : guardPagesPerStack_(guardPagesPerStack) {
#ifndef _WIN32
  maybeInstallGuardPageSignalHandler();
#endif
}

GuardPageAllocator::~GuardPageAllocator() = default;

unsigned char* GuardPageAllocator::allocate(size_t size) {
  if (guardPagesPerStack_ && !stackCache_) {
    stackCache_ =
        CacheManager::instance().getStackCache(size, guardPagesPerStack_);
  }

  if (stackCache_) {
    auto p = stackCache_->cache().borrow(size);
    if (p != nullptr) {
      return p;
    }
  }
  return fallbackAllocator_.allocate(size);
}

void GuardPageAllocator::deallocate(unsigned char* limit, size_t size) {
  if (!(stackCache_ && stackCache_->cache().giveBack(limit, size))) {
    fallbackAllocator_.deallocate(limit, size);
  }
}
} // namespace fibers
} // namespace folly
