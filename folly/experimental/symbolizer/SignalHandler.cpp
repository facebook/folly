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

// This is heavily inspired by the signal handler from google-glog

#include <folly/experimental/symbolizer/SignalHandler.h>

#include <signal.h>
#include <sys/types.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <ctime>
#include <mutex>
#include <vector>

#include <glog/logging.h>

#include <folly/ScopeGuard.h>
#include <folly/experimental/symbolizer/Symbolizer.h>
#include <folly/lang/ToAscii.h>
#include <folly/portability/SysSyscall.h>
#include <folly/portability/Unistd.h>

namespace folly {
namespace symbolizer {

#ifndef _WIN32

const unsigned long kAllFatalSignals = (1UL << SIGSEGV) | (1UL << SIGILL) |
    (1UL << SIGFPE) | (1UL << SIGABRT) | (1UL << SIGBUS) | (1UL << SIGTERM) |
    (1UL << SIGQUIT);

#endif

namespace {

/**
 * Fatal signal handler registry.
 */
class FatalSignalCallbackRegistry {
 public:
  FatalSignalCallbackRegistry();

  void add(SignalCallback func);
  void markInstalled();
  void run();

 private:
  std::atomic<bool> installed_;
  std::mutex mutex_;
  std::vector<SignalCallback> handlers_;
};

FatalSignalCallbackRegistry::FatalSignalCallbackRegistry()
    : installed_(false) {}

void FatalSignalCallbackRegistry::add(SignalCallback func) {
  std::lock_guard<std::mutex> lock(mutex_);
  CHECK(!installed_) << "FatalSignalCallbackRegistry::add may not be used "
                        "after installing the signal handlers.";
  handlers_.push_back(func);
}

void FatalSignalCallbackRegistry::markInstalled() {
  std::lock_guard<std::mutex> lock(mutex_);
  CHECK(!installed_.exchange(true))
      << "FatalSignalCallbackRegistry::markInstalled must be called "
      << "at most once";
}

void FatalSignalCallbackRegistry::run() {
  if (!installed_) {
    return;
  }

  for (auto& fn : handlers_) {
    fn();
  }
}

std::atomic<FatalSignalCallbackRegistry*> gFatalSignalCallbackRegistry{};

FatalSignalCallbackRegistry* getFatalSignalCallbackRegistry() {
  // Leak it so we don't have to worry about destruction order
  static FatalSignalCallbackRegistry* fatalSignalCallbackRegistry =
      new FatalSignalCallbackRegistry();

  return fatalSignalCallbackRegistry;
}

} // namespace

void addFatalSignalCallback(SignalCallback cb) {
  getFatalSignalCallbackRegistry()->add(cb);
}

void installFatalSignalCallbacks() {
  getFatalSignalCallbackRegistry()->markInstalled();
}

#ifndef _WIN32

namespace {

struct {
  int number;
  const char* name;
  struct sigaction oldAction;
} kFatalSignals[] = {
    {SIGSEGV, "SIGSEGV", {}},
    {SIGILL, "SIGILL", {}},
    {SIGFPE, "SIGFPE", {}},
    {SIGABRT, "SIGABRT", {}},
    {SIGBUS, "SIGBUS", {}},
    {SIGTERM, "SIGTERM", {}},
    {SIGQUIT, "SIGQUIT", {}},
    {0, nullptr, {}},
};

FOLLY_MAYBE_UNUSED void callPreviousSignalHandler(int signum) {
  // Restore disposition to old disposition, then kill ourselves with the same
  // signal. The signal will be blocked until we return from our handler,
  // then it will invoke the default handler and abort.
  for (auto p = kFatalSignals; p->name; ++p) {
    if (p->number == signum) {
      sigaction(signum, &p->oldAction, nullptr);
      raise(signum);
      return;
    }
  }

  // Not one of the signals we know about. Oh well. Reset to default.
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;
  sigaction(signum, &sa, nullptr);
  raise(signum);
}

#if FOLLY_USE_SYMBOLIZER

// Note: not thread-safe, but that's okay, as we only let one thread
// in our signal handler at a time.
//
// Leak it so we don't have to worry about destruction order
//
// Initialized by installFatalSignalHandler
SafeStackTracePrinter* gStackTracePrinter;

void print(StringPiece sp) {
  gStackTracePrinter->print(sp);
}

void flush() {
  gStackTracePrinter->flush();
}

void printDec(uint64_t val) {
  char buf[to_ascii_size_max_decimal<uint64_t>];
  size_t n = to_ascii_decimal(buf, val);
  gStackTracePrinter->print(StringPiece(buf, n));
}

void printHex(uint64_t val) {
  char buf[2 + to_ascii_size_max<16, uint64_t>];
  auto out = buf + 0;
  *out++ = '0';
  *out++ = 'x';
  out += to_ascii_lower<16>(out, buf + sizeof(buf), val);
  gStackTracePrinter->print(StringPiece(buf, out - buf));
}

void dumpTimeInfo() {
  SCOPE_EXIT { flush(); };
  time_t now = time(nullptr);
  print("*** Aborted at ");
  printDec(now);
  print(" (Unix time, try 'date -d @");
  printDec(now);
  print("') ***\n");
}

const char* sigill_reason(int si_code) {
  switch (si_code) {
    case ILL_ILLOPC:
      return "illegal opcode";
    case ILL_ILLOPN:
      return "illegal operand";
    case ILL_ILLADR:
      return "illegal addressing mode";
    case ILL_ILLTRP:
      return "illegal trap";
    case ILL_PRVOPC:
      return "privileged opcode";
    case ILL_PRVREG:
      return "privileged register";
    case ILL_COPROC:
      return "coprocessor error";
    case ILL_BADSTK:
      return "internal stack error";

    default:
      return nullptr;
  }
}

const char* sigfpe_reason(int si_code) {
  switch (si_code) {
    case FPE_INTDIV:
      return "integer divide by zero";
    case FPE_INTOVF:
      return "integer overflow";
    case FPE_FLTDIV:
      return "floating-point divide by zero";
    case FPE_FLTOVF:
      return "floating-point overflow";
    case FPE_FLTUND:
      return "floating-point underflow";
    case FPE_FLTRES:
      return "floating-point inexact result";
    case FPE_FLTINV:
      return "floating-point invalid operation";
    case FPE_FLTSUB:
      return "subscript out of range";

    default:
      return nullptr;
  }
}

const char* sigsegv_reason(int si_code) {
  switch (si_code) {
    case SEGV_MAPERR:
      return "address not mapped to object";
    case SEGV_ACCERR:
      return "invalid permissions for mapped object";

    default:
      return nullptr;
  }
}

const char* sigbus_reason(int si_code) {
  switch (si_code) {
    case BUS_ADRALN:
      return "invalid address alignment";
    case BUS_ADRERR:
      return "nonexistent physical address";
    case BUS_OBJERR:
      return "object-specific hardware error";

      // MCEERR_AR and MCEERR_AO: in sigaction(2) but not in headers.

    default:
      return nullptr;
  }
}

const char* sigtrap_reason(int si_code) {
  switch (si_code) {
    case TRAP_BRKPT:
      return "process breakpoint";
    case TRAP_TRACE:
      return "process trace trap";

      // TRAP_BRANCH and TRAP_HWBKPT: in sigaction(2) but not in headers.

    default:
      return nullptr;
  }
}

const char* sigchld_reason(int si_code) {
  switch (si_code) {
    case CLD_EXITED:
      return "child has exited";
    case CLD_KILLED:
      return "child was killed";
    case CLD_DUMPED:
      return "child terminated abnormally";
    case CLD_TRAPPED:
      return "traced child has trapped";
    case CLD_STOPPED:
      return "child has stopped";
    case CLD_CONTINUED:
      return "stopped child has continued";

    default:
      return nullptr;
  }
}

const char* sigio_reason(int si_code) {
  switch (si_code) {
    case POLL_IN:
      return "data input available";
    case POLL_OUT:
      return "output buffers available";
    case POLL_MSG:
      return "input message available";
    case POLL_ERR:
      return "I/O error";
    case POLL_PRI:
      return "high priority input available";
    case POLL_HUP:
      return "device disconnected";

    default:
      return nullptr;
  }
}

const char* signal_reason(int signum, int si_code) {
  switch (signum) {
    case SIGILL:
      return sigill_reason(si_code);
    case SIGFPE:
      return sigfpe_reason(si_code);
    case SIGSEGV:
      return sigsegv_reason(si_code);
    case SIGBUS:
      return sigbus_reason(si_code);
    case SIGTRAP:
      return sigtrap_reason(si_code);
    case SIGCHLD:
      return sigchld_reason(si_code);
    case SIGIO:
      return sigio_reason(si_code); // aka SIGPOLL

    default:
      return nullptr;
  }
}

void dumpSignalInfo(int signum, siginfo_t* siginfo) {
  SCOPE_EXIT { flush(); };
  // Get the signal name, if possible.
  const char* name = nullptr;
  for (auto p = kFatalSignals; p->name; ++p) {
    if (p->number == signum) {
      name = p->name;
      break;
    }
  }

  print("*** Signal ");
  printDec(signum);
  if (name) {
    print(" (");
    print(name);
    print(")");
  }

  print(" (");
  printHex(reinterpret_cast<uint64_t>(siginfo->si_addr));
  print(") received by PID ");
  printDec(getpid());
  print(" (pthread TID ");
  printHex((uint64_t)pthread_self());
#if defined(__linux__)
  print(") (linux TID ");
  printDec(syscall(__NR_gettid));
#elif defined(__FreeBSD__)
  long tid = 0;
  syscall(432, &tid);
  print(") (freebsd TID ");
  printDec(tid);
#endif

  // Kernel-sourced signals don't give us useful info for pid/uid.
  if (siginfo->si_code <= 0) {
    print(") (maybe from PID ");
    printDec(siginfo->si_pid);
    print(", UID ");
    printDec(siginfo->si_uid);
  }

  auto reason = signal_reason(signum, siginfo->si_code);

  print(") (code: ");
  // If we can't find a reason code make a best effort to print the (int) code.
  if (reason != nullptr) {
    print(reason);
  } else {
    if (siginfo->si_code < 0) {
      print("-");
      printDec(-siginfo->si_code);
    } else {
      printDec(siginfo->si_code);
    }
  }

  print("), stack trace: ***\n");
}

// On Linux, pthread_t is a pointer, so 0 is an invalid value, which we
// take to indicate "no thread in the signal handler".
//
// POSIX defines PTHREAD_NULL for this purpose, but that's not available.
constexpr pthread_t kInvalidThreadId = 0;

std::atomic<pthread_t> gSignalThread(kInvalidThreadId);
std::atomic<bool> gInRecursiveSignalHandler(false);

// Here be dragons.
void innerSignalHandler(int signum, siginfo_t* info, void* /* uctx */) {
  // First, let's only let one thread in here at a time.
  pthread_t myId = pthread_self();

  pthread_t prevSignalThread = kInvalidThreadId;
  while (!gSignalThread.compare_exchange_strong(prevSignalThread, myId)) {
    if (pthread_equal(prevSignalThread, myId)) {
      // First time here. Try to dump the stack trace without symbolization.
      // If we still fail, well, we're mightily screwed, so we do nothing the
      // next time around.
      if (!gInRecursiveSignalHandler.exchange(true)) {
        print("Entered fatal signal handler recursively. We're in trouble.\n");
        gStackTracePrinter->printStackTrace(false); // no symbolization
      }
      return;
    }

    // Wait a while, try again.
    timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100L * 1000 * 1000; // 100ms
    nanosleep(&ts, nullptr);

    prevSignalThread = kInvalidThreadId;
  }

  dumpTimeInfo();
  dumpSignalInfo(signum, info);
  gStackTracePrinter->printStackTrace(true); // with symbolization

  // Run user callbacks
  auto callbacks = gFatalSignalCallbackRegistry.load(std::memory_order_acquire);
  if (callbacks) {
    callbacks->run();
  }
}

namespace {
std::atomic<bool> gFatalSignalReceived{false};
} // namespace

void signalHandler(int signum, siginfo_t* info, void* uctx) {
  gFatalSignalReceived.store(true, std::memory_order_relaxed);

  int savedErrno = errno;
  SCOPE_EXIT {
    flush();
    errno = savedErrno;
  };
  innerSignalHandler(signum, info, uctx);

  gSignalThread = kInvalidThreadId;
  // Kill ourselves with the previous handler.
  callPreviousSignalHandler(signum);
}

#endif // FOLLY_USE_SYMBOLIZER

// Small sigaltstack size threshold.
// 51392 is known to cause the signal handler to stack overflow during
// symbolization of trivial async stacks (e.g [] { CHECK(false); co_return; }).
constexpr size_t kSmallSigAltStackSize = 51392;

FOLLY_MAYBE_UNUSED bool isSmallSigAltStackEnabled() {
  stack_t ss;
  if (sigaltstack(nullptr, &ss) != 0) {
    return false;
  }
  if ((ss.ss_flags & SS_DISABLE) != 0) {
    return false;
  }
  return ss.ss_size <= kSmallSigAltStackSize;
}

} // namespace

#endif // _WIN32

namespace {
std::atomic<bool> gAlreadyInstalled;
}

void installFatalSignalHandler(std::bitset<64> signals) {
  if (gAlreadyInstalled.exchange(true)) {
    // Already done.
    return;
  }

  // make sure gFatalSignalCallbackRegistry is created before we
  // install the fatal signal handler
  gFatalSignalCallbackRegistry.store(
      getFatalSignalCallbackRegistry(), std::memory_order_release);

#if FOLLY_USE_SYMBOLIZER
  // If a small sigaltstack is enabled (ex. Rust stdlib might use sigaltstack
  // to set a small stack), the default SafeStackTracePrinter would likely
  // stack overflow. Replace it with the unsafe self-allocate printer.
  bool useUnsafePrinter = kIsLinux && isSmallSigAltStackEnabled();
  if (useUnsafePrinter) {
#if FOLLY_HAVE_SWAPCONTEXT
    gStackTracePrinter = new UnsafeSelfAllocateStackTracePrinter();
#else
    // This environment does not support swapcontext, so always use
    // SafeStackTracePrinter.
    gStackTracePrinter = new SafeStackTracePrinter();
#endif // FOLLY_HAVE_SWAPCONTEXT
  } else {
    gStackTracePrinter = new SafeStackTracePrinter();
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  if (useUnsafePrinter) {
    // The signal handler is not async-signal-safe. Block all signals to
    // make it safer. But it's still unsafe.
    sigfillset(&sa.sa_mask);
  } else {
    sigemptyset(&sa.sa_mask);
  }
  // By default signal handlers are run on the signaled thread's stack.
  // In case of stack overflow running the SIGSEGV signal handler on
  // the same stack leads to another SIGSEGV and crashes the program.
  // Use SA_ONSTACK, so alternate stack is used (only if configured via
  // sigaltstack).
  // Golang also requires SA_ONSTACK. See:
  // https://golang.org/pkg/os/signal/#hdr-Go_programs_that_use_cgo_or_SWIG
  sa.sa_flags |= SA_SIGINFO | SA_ONSTACK;
  sa.sa_sigaction = &signalHandler;

  for (auto p = kFatalSignals; p->name; ++p) {
    if ((p->number < static_cast<int>(signals.size())) &&
        signals.test(p->number)) {
      CHECK_ERR(sigaction(p->number, &sa, &p->oldAction));
    }
  }
#endif // FOLLY_USE_SYMBOLIZER
}

bool fatalSignalReceived() {
#ifdef FOLLY_USE_SYMBOLIZER
  return gFatalSignalReceived.load(std::memory_order_relaxed);
#else
  return false;
#endif
}

} // namespace symbolizer
} // namespace folly
