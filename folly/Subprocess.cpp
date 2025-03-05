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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <folly/Subprocess.h>

#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <dlfcn.h>
#include <fcntl.h>

#include <algorithm>
#include <array>
#include <system_error>
#include <thread>

#include <boost/container/flat_set.hpp>
#include <boost/range/adaptors.hpp>

#include <folly/Conv.h>
#include <folly/Exception.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/io/Cursor.h>
#include <folly/lang/Assume.h>
#include <folly/logging/xlog.h>
#include <folly/portability/Dirent.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Stdlib.h>
#include <folly/portability/SysSyscall.h>
#include <folly/portability/Unistd.h>
#include <folly/system/AtFork.h>
#include <folly/system/Shell.h>

/// interceptors to work around:
///
/// https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/compiler-rt/lib/tsan/rtl/tsan_interceptors_posix.cpp
/// https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/compiler-rt/lib/sanitizer_common/sanitizer_signal_interceptors.inc

/// In sanitized builds, explicitly disable sanitization in the child process.
/// * Disable sanitizer function transformation, including __tsan_func_entry and
///   __tsan_func_exit hooks (under clang).
/// * Bypass sanitizer interceptors of libc/posix functions, including of vfork
///   and of all other libc/posix callees in the child process. Interceptors
///   look like __interceptor_trampoline_{name} for libc/posix function {name}.

#if __has_attribute(disable_sanitizer_instrumentation)
#define FOLLY_DETAIL_SUBPROCESS_RAW                  \
  __attribute__((                                    \
      noinline,                                      \
      no_sanitize("address", "undefined", "thread"), \
      disable_sanitizer_instrumentation))
#else
#define FOLLY_DETAIL_SUBPROCESS_RAW \
  __attribute__((noinline, no_sanitize("address", "undefined", "thread")))
#endif

constexpr int kExecFailure = 127;
constexpr int kChildFailure = 126;

namespace folly {

namespace detail {

SubprocessFdActionsList::SubprocessFdActionsList(
    span<value_type const> rep) noexcept
    : begin_{rep.data()}, end_{rep.data() + rep.size()} {
  [[maybe_unused]] auto lt = [](auto a, auto b) { return a.first < b.first; };
  assert(std::is_sorted(begin_, end_, lt));
  [[maybe_unused]] auto eq = [](auto a, auto b) { return a.first == b.first; };
  assert(std::adjacent_find(begin_, end_, eq) == end_);
}

FOLLY_DETAIL_SUBPROCESS_RAW
auto SubprocessFdActionsList::begin() const noexcept -> value_type const* {
  return begin_;
}
FOLLY_DETAIL_SUBPROCESS_RAW
auto SubprocessFdActionsList::end() const noexcept -> value_type const* {
  return end_;
}
FOLLY_DETAIL_SUBPROCESS_RAW
auto SubprocessFdActionsList::find(int fd) const noexcept -> int const* {
  auto lo = begin_;
  auto hi = end_;
  while (lo < hi) {
    auto mid = lo + (hi - lo) / 2;
    if (mid->first == fd) {
      return &mid->second;
    }
    if (mid->first < fd) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return nullptr;
}

// clang-format off
static inline constexpr auto subprocess_libc_soname =
    kIsLinux ? "libc.so.6" :
    kIsFreeBSD ? "libc.so.7" :
    kIsApple ? "/usr/lib/libc.dylib" :
    nullptr;
// clang-format on

template <typename Ret>
static Ret subprocess_libc_load(
    void* const handle, Ret const ptr, char const* const name) {
  return !kIsSanitize ? ptr : reinterpret_cast<Ret>(::dlsym(handle, name));
}

#define FOLLY_DETAIL_SUBPROCESS_LIBC_X_BASE(X) \
  X(_exit, _exit)                              \
  X(close, close)                              \
  X(dup2, dup2)                                \
  X(fcntl, fcntl)                              \
  X(pthread_sigmask, pthread_sigmask)          \
  X(signal, signal)                            \
  X(strtol, strtol)                            \
  X(vfork, vfork)                              \
  X(write, write)

#if defined(__BIONIC_INCLUDE_FORTIFY_HEADERS)
#define FOLLY_DETAIL_SUBPROCESS_LIBC_X_OPEN(X) X(open, __open_real)
#else
#define FOLLY_DETAIL_SUBPROCESS_LIBC_X_OPEN(X) X(open, open)
#endif

#if defined(__linux__)
#define FOLLY_DETAIL_SUBPROCESS_LIBC_X_PRCTL(X) X(prctl, prctl)
#else
#define FOLLY_DETAIL_SUBPROCESS_LIBC_X_PRCTL(X)
#endif

#define FOLLY_DETAIL_SUBPROCESS_LIBC_X(X) \
  FOLLY_DETAIL_SUBPROCESS_LIBC_X_BASE(X)  \
  FOLLY_DETAIL_SUBPROCESS_LIBC_X_OPEN(X)  \
  FOLLY_DETAIL_SUBPROCESS_LIBC_X_PRCTL(X)

#define FOLLY_DETAIL_SUBPROCESS_LIBC_FIELD_DECL(name, func) \
  static decltype(&::func) name;
#define FOLLY_DETAIL_SUBPROCESS_LIBC_FIELD_DEFN(name, func) \
  decltype(&::func) subprocess_libc::name;
#define FOLLY_DETAIL_SUBPROCESS_LIBC_INIT(name, func) \
  subprocess_libc::name = subprocess_libc_load(handle, &::func, #name);

struct subprocess_libc {
  FOLLY_DETAIL_SUBPROCESS_LIBC_X(FOLLY_DETAIL_SUBPROCESS_LIBC_FIELD_DECL)
};

FOLLY_DETAIL_SUBPROCESS_LIBC_X(FOLLY_DETAIL_SUBPROCESS_LIBC_FIELD_DEFN)

__attribute__((constructor(101))) static void subprocess_libc_init() {
  auto handle = !kIsSanitize
      ? nullptr
      : ::dlopen(subprocess_libc_soname, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
  assert(!kIsSanitize || !!handle);
  FOLLY_DETAIL_SUBPROCESS_LIBC_X(FOLLY_DETAIL_SUBPROCESS_LIBC_INIT)
}

#undef FOLLY_DETAIL_SUBPROCESS_LIBC_FIELD_DECL
#undef FOLLY_DETAIL_SUBPROCESS_LIBC_FIELD_DEFN
#undef FOLLY_DETAIL_SUBPROCESS_LIBC_INIT
#undef FOLLY_DETAIL_SUBPROCESS_LIBC_X
#undef FOLLY_DETAIL_SUBPROCESS_LIBC_X_PRCTL
#undef FOLLY_DETAIL_SUBPROCESS_LIBC_X_OPEN
#undef FOLLY_DETAIL_SUBPROCESS_LIBC_X_BASE

} // namespace detail

struct Subprocess::SpawnRawArgs {
  struct Scratch {
    std::vector<std::pair<int, int>> fdActions;

    explicit Scratch(Options const& options)
        : fdActions{options.fdActions_.begin(), options.fdActions_.end()} {
      std::sort(fdActions.begin(), fdActions.end());
    }
  };

  // from options
  char const* childDir{};
  bool closeOtherFds{};
#if defined(__linux__)
  cpu_set_t const* cpuSet{};
#endif
  DangerousPostForkPreExecCallback* dangerousPostForkPreExecCallback;
  bool detach{};
  detail::SubprocessFdActionsList fdActions;
  int parentDeathSignal{};
  bool processGroupLeader{};
  bool usePath{};

  // assigned explicitly
  char const* const* argv{};
  char const* const* envv{};
  char const* executable{};
  int errFd{};
  sigset_t oldSignals{};

  explicit SpawnRawArgs(Scratch const& scratch, Options const& options)
      : childDir{options.childDir_.empty() ? nullptr : options.childDir_.c_str()},
        closeOtherFds{options.closeOtherFds_},
#if defined(__linux__)
        cpuSet{get_pointer(options.cpuSet_)},
#endif
        dangerousPostForkPreExecCallback{
            options.dangerousPostForkPreExecCallback_},
        detach{options.detach_},
        fdActions{scratch.fdActions},
#if defined(__linux__)
        parentDeathSignal{options.parentDeathSignal_},
#endif
        processGroupLeader{options.processGroupLeader_},
        usePath{options.usePath_} {
    static_assert(std::is_standard_layout_v<Subprocess::SpawnRawArgs>);
    static_assert(std::is_trivially_destructible_v<Subprocess::SpawnRawArgs>);
  }
};

ProcessReturnCode ProcessReturnCode::make(int status) {
  if (!WIFEXITED(status) && !WIFSIGNALED(status)) {
    throw std::runtime_error(
        to<std::string>("Invalid ProcessReturnCode: ", status));
  }
  return ProcessReturnCode(status);
}

ProcessReturnCode::ProcessReturnCode(ProcessReturnCode&& p) noexcept
    : rawStatus_(p.rawStatus_) {
  p.rawStatus_ = ProcessReturnCode::RV_NOT_STARTED;
}

ProcessReturnCode& ProcessReturnCode::operator=(
    ProcessReturnCode&& p) noexcept {
  rawStatus_ = p.rawStatus_;
  p.rawStatus_ = ProcessReturnCode::RV_NOT_STARTED;
  return *this;
}

ProcessReturnCode::State ProcessReturnCode::state() const {
  if (rawStatus_ == RV_NOT_STARTED) {
    return NOT_STARTED;
  }
  if (rawStatus_ == RV_RUNNING) {
    return RUNNING;
  }
  if (WIFEXITED(rawStatus_)) {
    return EXITED;
  }
  if (WIFSIGNALED(rawStatus_)) {
    return KILLED;
  }
  assume_unreachable();
}

void ProcessReturnCode::enforce(State expected) const {
  State s = state();
  if (s != expected) {
    throw std::logic_error(to<std::string>(
        "Bad use of ProcessReturnCode; state is ", s, " expected ", expected));
  }
}

int ProcessReturnCode::exitStatus() const {
  enforce(EXITED);
  return WEXITSTATUS(rawStatus_);
}

int ProcessReturnCode::killSignal() const {
  enforce(KILLED);
  return WTERMSIG(rawStatus_);
}

bool ProcessReturnCode::coreDumped() const {
  enforce(KILLED);
  return WCOREDUMP(rawStatus_);
}

bool ProcessReturnCode::succeeded() const {
  return exited() && exitStatus() == 0;
}

std::string ProcessReturnCode::str() const {
  switch (state()) {
    case NOT_STARTED:
      return "not started";
    case RUNNING:
      return "running";
    case EXITED:
      return to<std::string>("exited with status ", exitStatus());
    case KILLED:
      return to<std::string>(
          "killed by signal ",
          killSignal(),
          (coreDumped() ? " (core dumped)" : ""));
  }
  assume_unreachable();
}

CalledProcessError::CalledProcessError(ProcessReturnCode rc)
    : SubprocessError(rc.str()), returnCode_(rc) {}

static inline std::string toSubprocessSpawnErrorMessage(
    char const* executable, int errCode, int errnoValue) {
  auto prefix = errCode == kExecFailure
      ? "failed to execute "
      : "error preparing to execute ";
  return to<std::string>(prefix, executable, ": ", errnoStr(errnoValue));
}

SubprocessSpawnError::SubprocessSpawnError(
    const char* executable, int errCode, int errnoValue)
    : SubprocessError(
          toSubprocessSpawnErrorMessage(executable, errCode, errnoValue)),
      errnoValue_(errnoValue) {}

namespace {

// Copy pointers to the given strings in a format suitable for posix_spawn
std::unique_ptr<const char*[]> cloneStrings(const std::vector<std::string>& s) {
  std::unique_ptr<const char*[]> d(new const char*[s.size() + 1]);
  for (size_t i = 0; i < s.size(); i++) {
    d[i] = s[i].c_str();
  }
  d[s.size()] = nullptr;
  return d;
}

// Check a wait() status, throw on non-successful
void checkStatus(ProcessReturnCode returnCode) {
  if (returnCode.state() != ProcessReturnCode::EXITED ||
      returnCode.exitStatus() != 0) {
    throw CalledProcessError(returnCode);
  }
}

} // namespace

Subprocess::Options& Subprocess::Options::fd(int fd, int action) {
  if (action == Subprocess::PIPE) {
    if (fd == 0) {
      action = Subprocess::PIPE_IN;
    } else if (fd == 1 || fd == 2) {
      action = Subprocess::PIPE_OUT;
    } else {
      throw std::invalid_argument(
          to<std::string>("Only fds 0, 1, 2 are valid for action=PIPE: ", fd));
    }
  }
  fdActions_[fd] = action;
  return *this;
}

Subprocess::Subprocess() = default;

Subprocess::Subprocess(
    const std::vector<std::string>& argv,
    const Options& options,
    const char* executable,
    const std::vector<std::string>* env)
    : destroyBehavior_(options.destroyBehavior_) {
  if (argv.empty()) {
    throw std::invalid_argument("argv must not be empty");
  }
  if (!executable) {
    executable = argv[0].c_str();
  }
  spawn(cloneStrings(argv), executable, options, env);
}

Subprocess::Subprocess(
    const std::string& cmd,
    const Options& options,
    const std::vector<std::string>* env)
    : destroyBehavior_(options.destroyBehavior_) {
  if (options.usePath_) {
    throw std::invalid_argument("usePath() not allowed when running in shell");
  }

  std::vector<std::string> argv = {"/bin/sh", "-c", cmd};
  spawn(cloneStrings(argv), argv[0].c_str(), options, env);
}

Subprocess Subprocess::fromExistingProcess(pid_t pid) {
  Subprocess sp;
  sp.pid_ = pid;
  sp.destroyBehavior_ = DestroyBehaviorLeak;
  sp.returnCode_ = ProcessReturnCode::makeRunning();
  return sp;
}

Subprocess::~Subprocess() {
  if (returnCode_.state() == ProcessReturnCode::RUNNING) {
    if (destroyBehavior_ == DestroyBehaviorFatal) {
      // Explicitly crash if we are destroyed without reaping the child process.
      //
      // If you are running into this crash, you are destroying a Subprocess
      // without cleaning up the child process first, which can leave behind a
      // zombie process on the system until the current process exits.  You may
      // want to use one of the following options instead when creating the
      // Subprocess:
      // - Options::detach()
      //   If you do not want to wait on the child process to complete, and do
      //   not care about its exit status, use detach().
      // - Options::killChildOnDestruction()
      //   If you want the child process to be automatically killed when the
      //   Subprocess is destroyed, use killChildOnDestruction() or
      //   terminateChildOnDestruction()
      XLOG(FATAL) << "Subprocess destroyed without reaping child";
    } else if (destroyBehavior_ == DestroyBehaviorLeak) {
      // Do nothing if we are destroyed without reaping the child process.
      XLOG(DBG) << "Subprocess destroyed without reaping child process";
    } else {
      // If we are killed without reaping the child process, explicitly
      // terminate/kill it and wait for it to exit.
      try {
        TimeoutDuration timeout(destroyBehavior_);
        terminateOrKill(timeout);
      } catch (const std::exception& ex) {
        XLOG(WARN) << "error terminating process in Subprocess destructor: "
                   << ex.what();
      }
    }
  }
}

namespace {

struct ChildErrorInfo {
  int errCode;
  int errnoValue;
};

} // namespace

[[noreturn]]
FOLLY_DETAIL_SUBPROCESS_RAW void Subprocess::childError(
    SpawnRawArgs const& args, int errCode, int errnoValue) {
  ChildErrorInfo info = {errCode, errnoValue};
  // Write the error information over the pipe to our parent process.
  // We can't really do anything else if this write call fails.
  ssize_t r = 0;
  do {
    r = detail::subprocess_libc::write(args.errFd, &info, sizeof(info));
  } while (r < 0 && errno == EINTR);
  // exit
  detail::subprocess_libc::_exit(errCode);
  __builtin_unreachable();
}

void Subprocess::setAllNonBlocking() {
  for (auto& p : pipes_) {
    int fd = p.pipe.fd();
    int flags = ::fcntl(fd, F_GETFL);
    checkUnixError(flags, "fcntl");
    int r = ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    checkUnixError(r, "fcntl");
  }
}

void Subprocess::spawn(
    std::unique_ptr<const char*[]> argv,
    const char* executable,
    const Options& optionsIn,
    const std::vector<std::string>* env) {
  if (optionsIn.usePath_ && env) {
    throw std::invalid_argument(
        "usePath() not allowed when overriding environment");
  }

  // Make a copy, we'll mutate options
  Options options(optionsIn);

  // On error, close all pipes_ (ignoring errors, but that seems fine here).
  auto pipesGuard = makeGuard([this] { pipes_.clear(); });

  // Create a pipe to use to receive error information from the child,
  // in case it fails before calling exec()
  int errFds[2];
#if FOLLY_HAVE_PIPE2
  checkUnixError(::pipe2(errFds, O_CLOEXEC), "pipe2");
#else
  checkUnixError(fileops::pipe(errFds), "pipe");
#endif
  SCOPE_EXIT {
    CHECK_ERR(fileops::close(errFds[0]));
    if (errFds[1] >= 0) {
      CHECK_ERR(fileops::close(errFds[1]));
    }
  };

#if !FOLLY_HAVE_PIPE2
  // Ask the child to close the read end of the error pipe.
  checkUnixError(fcntl(errFds[0], F_SETFD, FD_CLOEXEC), "set FD_CLOEXEC");
  // Set the close-on-exec flag on the write side of the pipe.
  // This way the pipe will be closed automatically in the child if execve()
  // succeeds.  If the exec fails the child can write error information to the
  // pipe.
  checkUnixError(fcntl(errFds[1], F_SETFD, FD_CLOEXEC), "set FD_CLOEXEC");
#endif

  // Perform the actual work of setting up pipes then forking and
  // executing the child.
  spawnInternal(std::move(argv), executable, options, env, errFds[1]);

  // After spawnInternal() returns the child is alive.  We have to be very
  // careful about throwing after this point.  We are inside the constructor,
  // so if we throw the Subprocess object will have never existed, and the
  // destructor will never be called.
  //
  // We should only throw if we got an error via the errFd, and we know the
  // child has exited and can be immediately waited for.  In all other cases,
  // we have no way of cleaning up the child.

  // Close writable side of the errFd pipe in the parent process
  CHECK_ERR(fileops::close(errFds[1]));
  errFds[1] = -1;

  // Read from the errFd pipe, to tell if the child ran into any errors before
  // calling exec()
  readChildErrorPipe(errFds[0], executable);

  // If we spawned a detached child, wait on the intermediate child process.
  // It always exits immediately.
  if (options.detach_) {
    wait();
  }

  // We have fully succeeded now, so release the guard on pipes_
  pipesGuard.dismiss();
}

void Subprocess::spawnInternal(
    std::unique_ptr<const char*[]> argv,
    const char* executable,
    Options& options,
    const std::vector<std::string>* env,
    int errFd) {
  // Parent work, pre-fork: create pipes
  std::vector<int> childFds;
  // Close all of the childFds as we leave this scope
  SCOPE_EXIT {
    // These are only pipes, closing them shouldn't fail
    for (int cfd : childFds) {
      CHECK_ERR(fileops::close(cfd));
    }
  };

  int r;
  for (auto& p : options.fdActions_) {
    if (p.second == PIPE_IN || p.second == PIPE_OUT) {
      int fds[2];
      // We're setting both ends of the pipe as close-on-exec. The child
      // doesn't need to reset the flag on its end, as we always dup2() the fd,
      // and dup2() fds don't share the close-on-exec flag.
#if FOLLY_HAVE_PIPE2
      // If possible, set close-on-exec atomically. Otherwise, a concurrent
      // Subprocess invocation can fork() between "pipe" and "fnctl",
      // causing FDs to leak.
      r = ::pipe2(fds, O_CLOEXEC);
      checkUnixError(r, "pipe2");
#else
      r = fileops::pipe(fds);
      checkUnixError(r, "pipe");
      r = fcntl(fds[0], F_SETFD, FD_CLOEXEC);
      checkUnixError(r, "set FD_CLOEXEC");
      r = fcntl(fds[1], F_SETFD, FD_CLOEXEC);
      checkUnixError(r, "set FD_CLOEXEC");
#endif
      pipes_.emplace_back();
      Pipe& pipe = pipes_.back();
      pipe.direction = p.second;
      int cfd;
      if (p.second == PIPE_IN) {
        // Child gets reading end
        pipe.pipe = folly::File(fds[1], /*ownsFd=*/true);
        cfd = fds[0];
      } else {
        pipe.pipe = folly::File(fds[0], /*ownsFd=*/true);
        cfd = fds[1];
      }
      p.second = cfd; // ensure it gets dup2()ed
      pipe.childFd = p.first;
      childFds.push_back(cfd);
    }
  }

  // This should already be sorted, as options.fdActions_ is
  DCHECK(std::is_sorted(pipes_.begin(), pipes_.end()));

  // Note that the const casts below are legit, per
  // http://pubs.opengroup.org/onlinepubs/009695399/functions/exec.html

  // Set up environment
  std::unique_ptr<const char*[]> envHolder;
  if (env) {
    envHolder = cloneStrings(*env);
  }

  // Block all signals around vfork; see http://ewontfix.com/7/.
  //
  // As the child may run in the same address space as the parent until
  // the actual execve() system call, any (custom) signal handlers that
  // the parent has might alter parent's memory if invoked in the child,
  // with undefined results.  So we block all signals in the parent before
  // vfork(), which will cause them to be blocked in the child as well (we
  // rely on the fact that Linux, just like all sane implementations, only
  // clones the calling thread).  Then, in the child, we reset all signals
  // to their default dispositions (while still blocked), and unblock them
  // (so the exec()ed process inherits the parent's signal mask)
  //
  // The parent also unblocks all signals as soon as vfork() returns.
  sigset_t allBlocked;
  r = sigfillset(&allBlocked);
  checkUnixError(r, "sigfillset");
  sigset_t oldSignals;

  r = pthread_sigmask(SIG_SETMASK, &allBlocked, &oldSignals);
  checkPosixError(r, "pthread_sigmask");
  SCOPE_EXIT {
    // Restore signal mask
    r = pthread_sigmask(SIG_SETMASK, &oldSignals, nullptr);
    CHECK_EQ(r, 0) << "pthread_sigmask: " << errnoStr(r); // shouldn't fail
  };

  SpawnRawArgs::Scratch scratch{options};
  SpawnRawArgs args{scratch, options};
  args.argv = argv.get();
  args.envv = env ? envHolder.get() : environ;
  args.executable = executable;
  args.errFd = errFd;
  args.oldSignals = oldSignals;

  // Child is alive.  We have to be very careful about throwing after this
  // point.  We are inside the constructor, so if we throw the Subprocess
  // object will have never existed, and the destructor will never be called.
  //
  // We should only throw if we got an error via the errFd, and we know the
  // child has exited and can be immediately waited for.  In all other cases,
  // we have no way of cleaning up the child.
  pid_ = spawnInternalDoFork(args);
  returnCode_ = ProcessReturnCode::makeRunning();
}

// With -Wclobbered, gcc complains about vfork potentially cloberring the
// childDir variable, even though we only use it on the child side of the
// vfork.

FOLLY_PUSH_WARNING
FOLLY_GCC_DISABLE_WARNING("-Wclobbered")
FOLLY_DETAIL_SUBPROCESS_RAW
pid_t Subprocess::spawnInternalDoFork(SpawnRawArgs const& args) {
  pid_t pid = detail::subprocess_libc::vfork();
  checkUnixError(pid, errno, "failed to fork");
  if (pid != 0) {
    return pid;
  }

  // From this point onward, we are in the child.

  // Fork a second time if detach_ was requested.
  // This must be done before signals are restored in prepareChild()
  if (args.detach) {
    pid = detail::subprocess_libc::vfork();
    if (pid == -1) {
      // Inform our parent process of the error so it can throw in the parent.
      childError(args, kChildFailure, errno);
    } else if (pid != 0) {
      // We are the intermediate process.  Exit immediately.
      // Our child will still inform the original parent of success/failure
      // through errFd.  The pid of the grandchild process never gets
      // propagated back up to the original parent.  In the future we could
      // potentially send it back using errFd if we needed to.
      detail::subprocess_libc::_exit(0);
    }
  }

  int errnoValue = prepareChild(args);
  if (errnoValue != 0) {
    childError(args, kChildFailure, errnoValue);
  }

  errnoValue = runChild(args);
  // If we get here, exec() failed.
  childError(args, kExecFailure, errnoValue);

  return 0; // unreachable
}
FOLLY_POP_WARNING

// If requested, close all other file descriptors.  Don't close
// any fds in options.fdActions_, and don't touch stdin, stdout, stderr.
// Ignore errors.
//
//
// This function is called in the child after fork but before exec so
// there is very little it can do. It cannot allocate memory and
// it cannot lock a mutex, just as if it were running in a signal
// handler.
FOLLY_DETAIL_SUBPROCESS_RAW
void Subprocess::closeInheritedFds(const SpawnRawArgs& args) {
#if defined(__linux__)
  int dirfd = detail::subprocess_libc::open("/proc/self/fd", O_RDONLY);
  if (dirfd != -1) {
    char buffer[32768];
    int res;
    while ((res = syscall(SYS_getdents64, dirfd, buffer, sizeof(buffer))) > 0) {
      // linux_dirent64 is part of the kernel ABI for the getdents64 system
      // call. It is currently the same as struct dirent64 in both glibc and
      // musl, but those are library specific and could change. linux_dirent64
      // is not defined in the standard set of Linux userspace headers
      // (/usr/include/linux)
      //
      // We do not use the POSIX interfaces (opendir, readdir, etc..) for
      // reading a directory since they may allocate memory / grab a lock, which
      // is unsafe in this context.
      FOLLY_PUSH_WARNING
      FOLLY_CLANG_DISABLE_WARNING("-Wzero-length-array")
      struct linux_dirent64 {
        uint64_t d_ino;
        int64_t d_off;
        uint16_t d_reclen;
        unsigned char d_type;
        char d_name[0];
      } const* entry;
      FOLLY_POP_WARNING
      for (int offset = 0; offset < res; offset += entry->d_reclen) {
        entry = reinterpret_cast<struct linux_dirent64*>(buffer + offset);
        if (entry->d_type != DT_LNK) {
          continue;
        }
        char* end_p = nullptr;
        errno = 0;
        int fd = static_cast<int>(
            detail::subprocess_libc::strtol(entry->d_name, &end_p, 10));
        if (errno == ERANGE || fd < 3 || end_p == entry->d_name) {
          continue;
        }
        if ((fd != dirfd) && (args.fdActions.find(fd) == nullptr)) {
          detail::subprocess_libc::close(fd);
        }
      }
    }
    detail::subprocess_libc::close(dirfd);
    return;
  }
#endif
  // If not running on Linux or if we failed to open /proc/self/fd, try to close
  // all possible open file descriptors.
  for (auto fd = sysconf(_SC_OPEN_MAX) - 1; fd >= 3; --fd) {
    if (args.fdActions.find(fd) == nullptr) {
      detail::subprocess_libc::close(fd);
    }
  }
}

FOLLY_DETAIL_SUBPROCESS_RAW
int Subprocess::prepareChild(SpawnRawArgs const& args) {
  // While all signals are blocked, we must reset their
  // dispositions to default.
  for (int sig = 1; sig < NSIG; ++sig) {
    detail::subprocess_libc::signal(sig, SIG_DFL);
  }

  {
    // Unblock signals; restore signal mask.
    int r = detail::subprocess_libc::pthread_sigmask(
        SIG_SETMASK, &args.oldSignals, nullptr);
    if (r != 0) {
      return r; // pthread_sigmask() returns an errno value
    }
  }

  // Change the working directory, if one is given
  if (args.childDir) {
    if (::chdir(args.childDir) == -1) {
      return errno;
    }
  }

#ifdef __linux__
  // Best effort
  if (args.cpuSet) {
    const auto& cpuSet = *args.cpuSet;
    ::sched_setaffinity(0, sizeof(cpuSet), &cpuSet);
  }
#endif

  // We don't have to explicitly close the parent's end of all pipes,
  // as they all have the FD_CLOEXEC flag set and will be closed at
  // exec time.

  // Redirect requested FDs to /dev/null or NUL
  // dup2 any explicitly specified FDs
  for (auto p : args.fdActions) {
    if (p.second == DEV_NULL) {
      // folly/portability/Fcntl provides an impl of open that will
      // map this to NUL on Windows.
      auto devNull =
          detail::subprocess_libc::open("/dev/null", O_RDWR | O_CLOEXEC);
      if (devNull == -1) {
        return errno;
      }
      // note: dup2 will not set CLOEXEC on the destination
      if (detail::subprocess_libc::dup2(devNull, p.first) == -1) {
        // explicit close on error to avoid leaking fds
        detail::subprocess_libc::close(devNull);
        return errno;
      }
      detail::subprocess_libc::close(devNull);
    } else if (p.second != p.first) {
      if (detail::subprocess_libc::dup2(p.second, p.first) == -1) {
        return errno;
      }
    }
  }

  if (args.closeOtherFds) {
    closeInheritedFds(args);
  }

#if defined(__linux__)
  // Opt to receive signal on parent death, if requested
  if (args.parentDeathSignal != 0) {
    const auto parentDeathSignal =
        static_cast<unsigned long>(args.parentDeathSignal);
    if (detail::subprocess_libc::prctl(
            PR_SET_PDEATHSIG, parentDeathSignal, 0, 0, 0) == -1) {
      return errno;
    }
  }
#endif

  if (args.processGroupLeader) {
#if !defined(__FreeBSD__)
    if (setpgrp() == -1) {
#else
    if (setpgrp(getpid(), getpgrp()) == -1) {
#endif
      return errno;
    }
  }

  // The user callback comes last, so that the child is otherwise all set up.
  if (args.dangerousPostForkPreExecCallback) {
    if (int error = (*args.dangerousPostForkPreExecCallback)()) {
      return error;
    }
  }

  return 0;
}

FOLLY_DETAIL_SUBPROCESS_RAW
int Subprocess::runChild(SpawnRawArgs const& args) {
  auto argv = const_cast<char* const*>(args.argv);
  auto envv = const_cast<char* const*>(args.envv);
  // Now, finally, exec.
  if (args.usePath) {
    ::execvp(args.executable, argv);
  } else {
    ::execve(args.executable, argv, envv);
  }
  return errno;
}

void Subprocess::readChildErrorPipe(int pfd, const char* executable) {
  ChildErrorInfo info;
  auto rc = readNoInt(pfd, &info, sizeof(info));
  if (rc == 0) {
    // No data means the child executed successfully, and the pipe
    // was closed due to the close-on-exec flag being set.
    return;
  } else if (rc != sizeof(ChildErrorInfo)) {
    // An error occurred trying to read from the pipe, or we got a partial read.
    // Neither of these cases should really occur in practice.
    //
    // We can't get any error data from the child in this case, and we don't
    // know if it is successfully running or not.  All we can do is to return
    // normally, as if the child executed successfully.  If something bad
    // happened the caller should at least get a non-normal exit status from
    // the child.
    XLOGF(
        ERR,
        "unexpected error trying to read from child error pipe rc={}, errno={}",
        rc,
        errno);
    return;
  }

  // We got error data from the child.  The child should exit immediately in
  // this case, so wait on it to clean up.
  wait();

  // Throw to signal the error
  throw SubprocessSpawnError(executable, info.errCode, info.errnoValue);
}

ProcessReturnCode Subprocess::poll(struct rusage* ru) {
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  DCHECK_GT(pid_, 0);
  int status;
  pid_t found = ::wait4(pid_, &status, WNOHANG, ru);
  // The spec guarantees that EINTR does not occur with WNOHANG, so the only
  // two remaining errors are ECHILD (other code reaped the child?), or
  // EINVAL (cosmic rays?), both of which merit an abort:
  PCHECK(found != -1) << "waitpid(" << pid_ << ", &status, WNOHANG)";
  if (found != 0) {
    // Though the child process had quit, this call does not close the pipes
    // since its descendants may still be using them.
    returnCode_ = ProcessReturnCode::make(status);
    pid_ = -1;
  }
  return returnCode_;
}

bool Subprocess::pollChecked() {
  if (poll().state() == ProcessReturnCode::RUNNING) {
    return false;
  }
  checkStatus(returnCode_);
  return true;
}

ProcessReturnCode Subprocess::wait() {
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  DCHECK_GT(pid_, 0);
  int status;
  pid_t found;
  do {
    found = ::waitpid(pid_, &status, 0);
  } while (found == -1 && errno == EINTR);
  // The only two remaining errors are ECHILD (other code reaped the
  // child?), or EINVAL (cosmic rays?), and both merit an abort:
  PCHECK(found != -1) << "waitpid(" << pid_ << ", &status, 0)";
  // Though the child process had quit, this call does not close the pipes
  // since its descendants may still be using them.
  DCHECK_EQ(found, pid_);
  returnCode_ = ProcessReturnCode::make(status);
  pid_ = -1;
  return returnCode_;
}

void Subprocess::waitChecked() {
  wait();
  checkStatus(returnCode_);
}

ProcessReturnCode Subprocess::waitTimeout(TimeoutDuration timeout) {
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  DCHECK_GT(pid_, 0) << "The subprocess has been waited already";

  auto pollUntil = std::chrono::steady_clock::now() + timeout;
  auto sleepDuration = std::chrono::milliseconds{2};
  constexpr auto maximumSleepDuration = std::chrono::milliseconds{100};

  for (;;) {
    // Always call waitpid once after the full timeout has elapsed.
    auto now = std::chrono::steady_clock::now();

    int status;
    pid_t found;
    do {
      found = ::waitpid(pid_, &status, WNOHANG);
    } while (found == -1 && errno == EINTR);
    PCHECK(found != -1) << "waitpid(" << pid_ << ", &status, WNOHANG)";
    if (found) {
      // Just on the safe side, make sure it's the actual pid we are waiting.
      DCHECK_EQ(found, pid_);
      returnCode_ = ProcessReturnCode::make(status);
      // Change pid_ to -1 to detect programming error like calling
      // this method multiple times.
      pid_ = -1;
      return returnCode_;
    }
    if (now > pollUntil) {
      // Timed out: still running().
      return returnCode_;
    }
    // The subprocess is still running, sleep for increasing periods of time.
    std::this_thread::sleep_for(sleepDuration);
    sleepDuration =
        std::min(maximumSleepDuration, sleepDuration + sleepDuration);
  }
}

void Subprocess::sendSignal(int signal) {
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  int r = ::kill(pid_, signal);
  checkUnixError(r, "kill");
}

ProcessReturnCode Subprocess::waitOrTerminateOrKill(
    TimeoutDuration waitTimeout, TimeoutDuration sigtermTimeout) {
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  DCHECK_GT(pid_, 0) << "The subprocess has been waited already";

  this->waitTimeout(waitTimeout);

  if (returnCode_.running()) {
    return terminateOrKill(sigtermTimeout);
  }
  return returnCode_;
}

ProcessReturnCode Subprocess::terminateOrKill(TimeoutDuration sigtermTimeout) {
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  DCHECK_GT(pid_, 0) << "The subprocess has been waited already";

  if (sigtermTimeout > TimeoutDuration(0)) {
    // 1. Send SIGTERM to kill the process
    terminate();
    // 2. check whether subprocess has terminated using non-blocking waitpid
    waitTimeout(sigtermTimeout);
    if (!returnCode_.running()) {
      return returnCode_;
    }
  }

  // 3. If we are at this point, we have waited enough time after
  // sending SIGTERM, we have to use nuclear option SIGKILL to kill
  // the subprocess.
  XLOGF(INFO, "Send SIGKILL to {}", pid_);
  kill();
  // 4. SIGKILL should kill the process otherwise there must be
  // something seriously wrong, just use blocking wait to wait for the
  // subprocess to finish.
  return wait();
}

pid_t Subprocess::pid() const {
  return pid_;
}

namespace {

ByteRange queueFront(const IOBufQueue& queue) {
  auto* p = queue.front();
  if (!p) {
    return ByteRange{};
  }
  return io::Cursor(p).peekBytes();
}

// fd write
bool handleWrite(int fd, IOBufQueue& queue) {
  for (;;) {
    auto b = queueFront(queue);
    if (b.empty()) {
      return true; // EOF
    }

    ssize_t n = writeNoInt(fd, b.data(), b.size());
    if (n == -1 && errno == EAGAIN) {
      return false;
    }
    checkUnixError(n, "write");
    queue.trimStart(n);
  }
}

// fd read
bool handleRead(int fd, IOBufQueue& queue) {
  for (;;) {
    auto p = queue.preallocate(100, 65000);
    ssize_t n = readNoInt(fd, p.first, p.second);
    if (n == -1 && errno == EAGAIN) {
      return false;
    }
    checkUnixError(n, "read");
    if (n == 0) {
      return true;
    }
    queue.postallocate(n);
  }
}

bool discardRead(int fd) {
  static const size_t bufSize = 65000;
  // Thread unsafe, but it doesn't matter.
  static std::unique_ptr<char[]> buf(new char[bufSize]);

  for (;;) {
    ssize_t n = readNoInt(fd, buf.get(), bufSize);
    if (n == -1 && errno == EAGAIN) {
      return false;
    }
    checkUnixError(n, "read");
    if (n == 0) {
      return true;
    }
  }
}

} // namespace

std::pair<std::string, std::string> Subprocess::communicate(StringPiece input) {
  IOBufQueue inputQueue;
  inputQueue.wrapBuffer(input.data(), input.size());

  auto outQueues = communicateIOBuf(std::move(inputQueue));
  auto outBufs =
      std::make_pair(outQueues.first.move(), outQueues.second.move());
  std::pair<std::string, std::string> out;
  if (outBufs.first) {
    outBufs.first->coalesce();
    out.first.assign(
        reinterpret_cast<const char*>(outBufs.first->data()),
        outBufs.first->length());
  }
  if (outBufs.second) {
    outBufs.second->coalesce();
    out.second.assign(
        reinterpret_cast<const char*>(outBufs.second->data()),
        outBufs.second->length());
  }
  return out;
}

std::pair<IOBufQueue, IOBufQueue> Subprocess::communicateIOBuf(
    IOBufQueue input) {
  // If the user supplied a non-empty input buffer, make sure
  // that stdin is a pipe so we can write the data.
  if (!input.empty()) {
    // findByChildFd() will throw std::invalid_argument if no pipe for
    // STDIN_FILENO exists
    findByChildFd(STDIN_FILENO);
  }

  std::pair<IOBufQueue, IOBufQueue> out;

  auto readCallback = [&](int pfd, int cfd) -> bool {
    if (cfd == STDOUT_FILENO) {
      return handleRead(pfd, out.first);
    } else if (cfd == STDERR_FILENO) {
      return handleRead(pfd, out.second);
    } else {
      // Don't close the file descriptor, the child might not like SIGPIPE,
      // just read and throw the data away.
      return discardRead(pfd);
    }
  };

  auto writeCallback = [&](int pfd, int cfd) -> bool {
    if (cfd == STDIN_FILENO) {
      return handleWrite(pfd, input);
    } else {
      // If we don't want to write to this fd, just close it.
      return true;
    }
  };

  communicate(std::move(readCallback), std::move(writeCallback));

  return out;
}

void Subprocess::communicate(
    FdCallback readCallback, FdCallback writeCallback) {
  // This serves to prevent wait() followed by communicate(), but if you
  // legitimately need that, send a patch to delete this line.
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  setAllNonBlocking();

  std::vector<pollfd> fds;
  fds.reserve(pipes_.size());
  std::vector<size_t> toClose; // indexes into pipes_
  toClose.reserve(pipes_.size());

  while (!pipes_.empty()) {
    fds.clear();
    toClose.clear();

    for (auto& p : pipes_) {
      pollfd pfd;
      pfd.fd = p.pipe.fd();
      // Yes, backwards, PIPE_IN / PIPE_OUT are defined from the
      // child's point of view.
      if (!p.enabled) {
        // Still keeping fd in watched set so we get notified of POLLHUP /
        // POLLERR
        pfd.events = 0;
      } else if (p.direction == PIPE_IN) {
        pfd.events = POLLOUT;
      } else {
        pfd.events = POLLIN;
      }
      fds.push_back(pfd);
    }

    int r;
    do {
      r = ::poll(fds.data(), fds.size(), -1);
    } while (r == -1 && errno == EINTR);
    checkUnixError(r, "poll");

    for (size_t i = 0; i < pipes_.size(); ++i) {
      auto& p = pipes_[i];
      auto parentFd = p.pipe.fd();
      DCHECK_EQ(fds[i].fd, parentFd);
      short events = fds[i].revents;

      bool closed = false;
      if (events & POLLOUT) {
        DCHECK(!(events & POLLIN));
        if (writeCallback(parentFd, p.childFd)) {
          toClose.push_back(i);
          closed = true;
        }
      }

      // Call read callback on POLLHUP, to give it a chance to read (and act
      // on) end of file
      if (events & (POLLIN | POLLHUP)) {
        DCHECK(!(events & POLLOUT));
        if (readCallback(parentFd, p.childFd)) {
          toClose.push_back(i);
          closed = true;
        }
      }

      if ((events & (POLLHUP | POLLERR)) && !closed) {
        toClose.push_back(i);
      }
    }

    // Close the fds in reverse order so the indexes hold after erase()
    for (int idx : boost::adaptors::reverse(toClose)) {
      auto pos = pipes_.begin() + idx;
      pos->pipe.close(); // Throws on error
      pipes_.erase(pos);
    }
  }
}

void Subprocess::enableNotifications(int childFd, bool enabled) {
  pipes_[findByChildFd(childFd)].enabled = enabled;
}

bool Subprocess::notificationsEnabled(int childFd) const {
  return pipes_[findByChildFd(childFd)].enabled;
}

size_t Subprocess::findByChildFd(int childFd) const {
  auto pos = std::lower_bound(
      pipes_.begin(), pipes_.end(), childFd, [](const Pipe& pipe, int fd) {
        return pipe.childFd < fd;
      });
  if (pos == pipes_.end() || pos->childFd != childFd) {
    throw std::invalid_argument(
        folly::to<std::string>("child fd not found ", childFd));
  }
  return pos - pipes_.begin();
}

void Subprocess::closeParentFd(int childFd) {
  int idx = findByChildFd(childFd);
  pipes_[idx].pipe.close(); // May throw
  pipes_.erase(pipes_.begin() + idx);
}

std::vector<Subprocess::ChildPipe> Subprocess::takeOwnershipOfPipes() {
  std::vector<Subprocess::ChildPipe> pipes;
  for (auto& p : pipes_) {
    pipes.emplace_back(p.childFd, std::move(p.pipe));
  }
  // release memory
  std::vector<Pipe>().swap(pipes_);
  return pipes;
}

namespace {

class Initializer {
 public:
  Initializer() {
    // We like EPIPE, thanks.
    ::signal(SIGPIPE, SIG_IGN);
  }
};

Initializer initializer;

} // namespace

} // namespace folly
