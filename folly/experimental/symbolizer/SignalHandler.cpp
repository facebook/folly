/*
 * Copyright 2013 Facebook, Inc.
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

// This is heavily inspired by the signal handler from google-glog

#include "folly/experimental/symbolizer/SignalHandler.h"

#include <sys/types.h>
#include <atomic>
#include <ctime>
#include <mutex>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <vector>

#include <glog/logging.h>

#include "folly/Conv.h"
#include "folly/FileUtil.h"
#include "folly/Portability.h"
#include "folly/ScopeGuard.h"
#include "folly/experimental/symbolizer/Symbolizer.h"

namespace folly { namespace symbolizer {

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
  : installed_(false) {
}

void FatalSignalCallbackRegistry::add(SignalCallback func) {
  std::lock_guard<std::mutex> lock(mutex_);
  CHECK(!installed_)
    << "FatalSignalCallbackRegistry::add may not be used "
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
    return;  // Shouldn't happen
  }

  for (auto& fn : handlers_) {
    fn();
  }
}

// Leak it so we don't have to worry about destruction order
FatalSignalCallbackRegistry* gFatalSignalCallbackRegistry =
  new FatalSignalCallbackRegistry;

struct {
  int number;
  const char* name;
  struct sigaction oldAction;
} kFatalSignals[] = {
  { SIGSEGV, "SIGSEGV" },
  { SIGILL,  "SIGILL"  },
  { SIGFPE,  "SIGFPE"  },
  { SIGABRT, "SIGABRT" },
  { SIGBUS,  "SIGBUS"  },
  { SIGTERM, "SIGTERM" },
  { 0,       nullptr   }
};

void callPreviousSignalHandler(int signum) {
  // Restore disposition to old disposition, then kill ourselves with the same
  // signal. The signal will be blocked until we return from our handler,
  // then it will invoke the default handler and abort.
  for (auto p = kFatalSignals; p->name; ++p) {
    if (p->number == signum) {
      sigaction(signum, &p->oldAction, nullptr);
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

void printDec(uint64_t val) {
  char buf[20];
  uint32_t n = uint64ToBufferUnsafe(val, buf);
  writeFull(STDERR_FILENO, buf, n);
}

const char kHexChars[] = "0123456789abcdef";
void printHex(uint64_t val) {
  // TODO(tudorb): Add this to folly/Conv.h
  char buf[2 + 2 * sizeof(uint64_t)];  // "0x" prefix, 2 digits for each byte

  char* end = buf + sizeof(buf);
  char* p = end;
  do {
    *--p = kHexChars[val & 0x0f];
    val >>= 4;
  } while (val != 0);
  *--p = 'x';
  *--p = '0';

  writeFull(STDERR_FILENO, p, end - p);
}

void print(StringPiece sp) {
  writeFull(STDERR_FILENO, sp.data(), sp.size());
}

void dumpTimeInfo() {
  SCOPE_EXIT { fsyncNoInt(STDERR_FILENO); };
  time_t now = time(nullptr);
  print("*** Aborted at ");
  printDec(now);
  print(" (Unix time, try 'date -d @");
  printDec(now);
  print("') ***\n");
}

void dumpSignalInfo(int signum, siginfo_t* siginfo) {
  SCOPE_EXIT { fsyncNoInt(STDERR_FILENO); };
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
  print(" (TID ");
  printHex((uint64_t)pthread_self());
  print("), stack trace: ***\n");
}

void dumpStackTrace() {
  SCOPE_EXIT { fsyncNoInt(STDERR_FILENO); };
  // Get and symbolize stack trace
  constexpr size_t kMaxStackTraceDepth = 100;
  FrameArray<kMaxStackTraceDepth> addresses;

  // Skip the getStackTrace frame
  if (!getStackTrace(addresses)) {
    print("(error retrieving stack trace)\n");
  } else {
    Symbolizer symbolizer;
    symbolizer.symbolize(addresses);

    FDSymbolizePrinter printer(STDERR_FILENO);
    printer.print(addresses);
  }
}

std::atomic<pthread_t*> gSignalThread;

// Here be dragons.
void innerSignalHandler(int signum, siginfo_t* info, void* uctx) {
  // First, let's only let one thread in here at a time.
  pthread_t myId = pthread_self();

  pthread_t* prevSignalThread = nullptr;
  while (!gSignalThread.compare_exchange_strong(prevSignalThread, &myId)) {
    if (pthread_equal(*prevSignalThread, myId)) {
      print("Entered fatal signal handler recursively. We're in trouble.\n");
      return;
    }

    // Wait a while, try again.
    timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100L * 1000 * 1000;  // 100ms
    nanosleep(&ts, nullptr);

    prevSignalThread = nullptr;
  }

  dumpTimeInfo();
  dumpSignalInfo(signum, info);
  dumpStackTrace();

  // Run user callbacks
  gFatalSignalCallbackRegistry->run();
}

void signalHandler(int signum, siginfo_t* info, void* uctx) {
  SCOPE_EXIT { fsyncNoInt(STDERR_FILENO); };
  innerSignalHandler(signum, info, uctx);

  gSignalThread = nullptr;
  // Kill ourselves with the previous handler.
  callPreviousSignalHandler(signum);
}

}  // namespace

void addFatalSignalCallback(SignalCallback cb) {
  gFatalSignalCallbackRegistry->add(cb);
}

namespace {

std::atomic<bool> gAlreadyInstalled;

}  // namespace

void installFatalSignalHandler() {
  if (gAlreadyInstalled.exchange(true)) {
    // Already done.
    return;
  }

  gFatalSignalCallbackRegistry->markInstalled();

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  sa.sa_flags |= SA_SIGINFO;
  sa.sa_sigaction = &signalHandler;

  for (auto p = kFatalSignals; p->name; ++p) {
    CHECK_ERR(sigaction(p->number, &sa, &p->oldAction));
  }
}

}}  // namespaces

