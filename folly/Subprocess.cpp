/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/Subprocess.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <wait.h>

#include <array>
#include <algorithm>
#include <system_error>

#include <boost/container/flat_set.hpp>
#include <boost/range/adaptors.hpp>

#include <glog/logging.h>

#include "folly/Conv.h"
#include "folly/ScopeGuard.h"
#include "folly/String.h"
#include "folly/experimental/io/Cursor.h"

extern char** environ;

namespace folly {

ProcessReturnCode::State ProcessReturnCode::state() const {
  if (rawStatus_ == RV_NOT_STARTED) return NOT_STARTED;
  if (rawStatus_ == RV_RUNNING) return RUNNING;
  if (WIFEXITED(rawStatus_)) return EXITED;
  if (WIFSIGNALED(rawStatus_)) return KILLED;
  throw std::runtime_error(to<std::string>(
      "Invalid ProcessReturnCode: ", rawStatus_));
}

void ProcessReturnCode::enforce(State s) const {
  if (state() != s) {
    throw std::logic_error(to<std::string>("Invalid state ", s));
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

std::string ProcessReturnCode::str() const {
  switch (state()) {
  case NOT_STARTED:
    return "not started";
  case RUNNING:
    return "running";
  case EXITED:
    return to<std::string>("exited with status ", exitStatus());
  case KILLED:
    return to<std::string>("killed by signal ", killSignal(),
                           (coreDumped() ? " (core dumped)" : ""));
  }
  CHECK(false);  // unreached
}

CalledProcessError::CalledProcessError(ProcessReturnCode rc)
  : returnCode_(rc),
    what_(returnCode_.str()) {
}

namespace {

// Copy pointers to the given strings in a format suitable for posix_spawn
std::unique_ptr<const char*[]> cloneStrings(const std::vector<std::string>& s) {
  std::unique_ptr<const char*[]> d(new const char*[s.size() + 1]);
  for (int i = 0; i < s.size(); i++) {
    d[i] = s[i].c_str();
  }
  d[s.size()] = nullptr;
  return d;
}

// Helper to throw std::system_error
void throwSystemError(int err, const char* msg) __attribute__((noreturn));
void throwSystemError(int err, const char* msg) {
  throw std::system_error(err, std::system_category(), msg);
}

// Helper to throw std::system_error from errno
void throwSystemError(const char* msg) __attribute__((noreturn));
void throwSystemError(const char* msg) {
  throwSystemError(errno, msg);
}

// Check a Posix return code (0 on success, error number on error), throw
// on error.
void checkPosixError(int err, const char* msg) {
  if (err != 0) {
    throwSystemError(err, msg);
  }
}

// Check a traditional Uinx return code (-1 and sets errno on error), throw
// on error.
void checkUnixError(ssize_t ret, const char* msg) {
  if (ret == -1) {
    throwSystemError(msg);
  }
}
void checkUnixError(ssize_t ret, int savedErrno, const char* msg) {
  if (ret == -1) {
    throwSystemError(savedErrno, msg);
  }
}

// Check a wait() status, throw on non-successful
void checkStatus(ProcessReturnCode returnCode) {
  if (returnCode.state() != ProcessReturnCode::EXITED ||
      returnCode.exitStatus() != 0) {
    throw CalledProcessError(returnCode);
  }
}

}  // namespace

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

Subprocess::Subprocess(
    const std::vector<std::string>& argv,
    const Options& options,
    const char* executable,
    const std::vector<std::string>* env)
  : pid_(-1),
    returnCode_(RV_NOT_STARTED) {
  if (argv.empty()) {
    throw std::invalid_argument("argv must not be empty");
  }
  if (!executable) executable = argv[0].c_str();
  spawn(cloneStrings(argv), executable, options, env);
}

Subprocess::Subprocess(
    const std::string& cmd,
    const Options& options,
    const std::vector<std::string>* env)
  : pid_(-1),
    returnCode_(RV_NOT_STARTED) {
  if (options.usePath_) {
    throw std::invalid_argument("usePath() not allowed when running in shell");
  }
  const char* shell = getenv("SHELL");
  if (!shell) {
    shell = "/bin/sh";
  }

  std::unique_ptr<const char*[]> argv(new const char*[4]);
  argv[0] = shell;
  argv[1] = "-c";
  argv[2] = cmd.c_str();
  argv[3] = nullptr;
  spawn(std::move(argv), shell, options, env);
}

Subprocess::~Subprocess() {
  CHECK_NE(returnCode_.state(), ProcessReturnCode::RUNNING)
    << "Subprocess destroyed without reaping child";
}

namespace {
void closeChecked(int fd) {
  checkUnixError(::close(fd), "close");
}
}  // namespace

void Subprocess::closeAll() {
  for (auto& p : pipes_) {
    closeChecked(p.parentFd);
  }
  pipes_.clear();
}

void Subprocess::setAllNonBlocking() {
  for (auto& p : pipes_) {
    int fd = p.parentFd;
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

  // Parent work, pre-fork: create pipes
  std::vector<int> childFds;
  for (auto& p : options.fdActions_) {
    if (p.second == PIPE_IN || p.second == PIPE_OUT) {
      int fds[2];
      int r = ::pipe(fds);
      checkUnixError(r, "pipe");
      PipeInfo pinfo;
      pinfo.direction = p.second;
      int cfd;
      if (p.second == PIPE_IN) {
        // Child gets reading end
        pinfo.parentFd = fds[1];
        cfd = fds[0];
      } else {
        pinfo.parentFd = fds[0];
        cfd = fds[1];
      }
      p.second = cfd;  // ensure it gets dup2()ed
      pinfo.childFd = p.first;
      childFds.push_back(cfd);
      pipes_.push_back(pinfo);
    }
  }

  // This should already be sorted, as options.fdActions_ is
  DCHECK(std::is_sorted(pipes_.begin(), pipes_.end()));

  // Note that the const casts below are legit, per
  // http://pubs.opengroup.org/onlinepubs/009695399/functions/exec.html

  char** argVec = const_cast<char**>(argv.get());

  // Set up environment
  std::unique_ptr<const char*[]> envHolder;
  char** envVec;
  if (env) {
    envHolder = cloneStrings(*env);
    envVec = const_cast<char**>(envHolder.get());
  } else {
    envVec = environ;
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
  int r = ::sigfillset(&allBlocked);
  checkUnixError(r, "sigfillset");
  sigset_t oldSignals;
  r = pthread_sigmask(SIG_SETMASK, &allBlocked, &oldSignals);
  checkPosixError(r, "pthread_sigmask");

  pid_t pid = vfork();
  if (pid == 0) {
    // While all signals are blocked, we must reset their
    // dispositions to default.
    for (int sig = 1; sig < NSIG; ++sig) {
      ::signal(sig, SIG_DFL);
    }
    // Unblock signals; restore signal mask.
    int r = pthread_sigmask(SIG_SETMASK, &oldSignals, nullptr);
    if (r != 0) abort();

    runChild(executable, argVec, envVec, options);
    // This should never return, but there's nothing else we can do here.
    abort();
  }
  // In parent.  We want to restore the signal mask even if vfork fails,
  // so we'll save errno here, restore the signal mask, and only then
  // throw.
  int savedErrno = errno;

  // Restore signal mask; do this even if vfork fails!
  // We only check for errors from pthread_sigmask after we recorded state
  // that the child is alive, so we know to reap it.
  r = pthread_sigmask(SIG_SETMASK, &oldSignals, nullptr);
  checkUnixError(pid, savedErrno, "vfork");

  // Child is alive
  pid_ = pid;
  returnCode_ = ProcessReturnCode(RV_RUNNING);

  // Parent work, post-fork: close child's ends of pipes
  for (int f : childFds) {
    closeChecked(f);
  }

  checkPosixError(r, "pthread_sigmask");
}

namespace {

// Checked version of close() to use in the child: abort() on error
void childClose(int fd) {
  int r = ::close(fd);
  if (r == -1) abort();
}

// Checked version of dup2() to use in the child: abort() on error
void childDup2(int oldfd, int newfd) {
  int r = ::dup2(oldfd, newfd);
  if (r == -1) abort();
}

}  // namespace

void Subprocess::runChild(const char* executable,
                          char** argv, char** env,
                          const Options& options) const {
  // Close parent's ends of all pipes
  for (auto& p : pipes_) {
    childClose(p.parentFd);
  }

  // Close all fds that we're supposed to close.
  // Note that we're ignoring errors here, in case some of these
  // fds were set to close on exec.
  for (auto& p : options.fdActions_) {
    if (p.second == CLOSE) {
      ::close(p.first);
    } else {
      childDup2(p.second, p.first);
    }
  }

  // If requested, close all other file descriptors.  Don't close
  // any fds in options.fdActions_, and don't touch stdin, stdout, stderr.
  // Ignore errors.
  if (options.closeOtherFds_) {
    for (int fd = getdtablesize() - 1; fd >= 3; --fd) {
      if (options.fdActions_.count(fd) == 0) {
        ::close(fd);
      }
    }
  }

  // Now, finally, exec.
  int r;
  if (options.usePath_) {
    ::execvp(executable, argv);
  } else {
    ::execve(executable, argv, env);
  }

  // If we're here, something's wrong.
  abort();
}

ProcessReturnCode Subprocess::poll() {
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  DCHECK_GT(pid_, 0);
  int status;
  pid_t found = ::waitpid(pid_, &status, WNOHANG);
  checkUnixError(found, "waitpid");
  if (found != 0) {
    returnCode_ = ProcessReturnCode(status);
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
  checkUnixError(found, "waitpid");
  DCHECK_EQ(found, pid_);
  returnCode_ = ProcessReturnCode(status);
  return returnCode_;
}

void Subprocess::waitChecked() {
  wait();
  checkStatus(returnCode_);
}

void Subprocess::sendSignal(int signal) {
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  int r = ::kill(pid_, signal);
  checkUnixError(r, "kill");
}

namespace {
void setNonBlocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL);
  checkUnixError(flags, "fcntl");
  int r = ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  checkUnixError(r, "fcntl");
}

std::pair<const uint8_t*, size_t> queueFront(const IOBufQueue& queue) {
  auto* p = queue.front();
  if (!p) return std::make_pair(nullptr, 0);
  return io::Cursor(p).peek();
}

// fd write
bool handleWrite(int fd, IOBufQueue& queue) {
  for (;;) {
    auto p = queueFront(queue);
    if (p.second == 0) {
      return true;  // EOF
    }

    ssize_t n;
    do {
      n = ::write(fd, p.first, p.second);
    } while (n == -1 && errno == EINTR);
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
    ssize_t n;
    do {
      n = ::read(fd, p.first, p.second);
    } while (n == -1 && errno == EINTR);
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
    ssize_t n;
    do {
      n = ::read(fd, buf.get(), bufSize);
    } while (n == -1 && errno == EINTR);
    if (n == -1 && errno == EAGAIN) {
      return false;
    }
    checkUnixError(n, "read");
    if (n == 0) {
      return true;
    }
  }
}

}  // namespace

std::pair<std::string, std::string> Subprocess::communicate(
    const CommunicateFlags& flags,
    StringPiece data) {
  IOBufQueue dataQueue;
  dataQueue.wrapBuffer(data.data(), data.size());

  auto outQueues = communicateIOBuf(flags, std::move(dataQueue));
  auto outBufs = std::make_pair(outQueues.first.move(),
                                outQueues.second.move());
  std::pair<std::string, std::string> out;
  if (outBufs.first) {
    outBufs.first->coalesce();
    out.first.assign(reinterpret_cast<const char*>(outBufs.first->data()),
                     outBufs.first->length());
  }
  if (outBufs.second) {
    outBufs.second->coalesce();
    out.second.assign(reinterpret_cast<const char*>(outBufs.second->data()),
                     outBufs.second->length());
  }
  return out;
}

std::pair<IOBufQueue, IOBufQueue> Subprocess::communicateIOBuf(
    const CommunicateFlags& flags,
    IOBufQueue data) {
  std::pair<IOBufQueue, IOBufQueue> out;

  auto readCallback = [&] (int pfd, int cfd) {
    if (cfd == 1 && flags.readStdout_) {
      return handleRead(pfd, out.first);
    } else if (cfd == 2 && flags.readStderr_) {
      return handleRead(pfd, out.second);
    } else {
      // Don't close the file descriptor, the child might not like SIGPIPE,
      // just read and throw the data away.
      return discardRead(pfd);
    }
  };

  auto writeCallback = [&] (int pfd, int cfd) {
    if (cfd == 0 && flags.writeStdin_) {
      return handleWrite(pfd, data);
    } else {
      // If we don't want to write to this fd, just close it.
      return false;
    }
  };

  communicate(std::move(readCallback), std::move(writeCallback));

  return out;
}

void Subprocess::communicate(FdCallback readCallback,
                             FdCallback writeCallback) {
  returnCode_.enforce(ProcessReturnCode::RUNNING);
  setAllNonBlocking();

  std::vector<pollfd> fds;
  fds.reserve(pipes_.size());
  std::vector<int> toClose;
  toClose.reserve(pipes_.size());

  while (!pipes_.empty()) {
    fds.clear();
    toClose.clear();

    for (auto& p : pipes_) {
      pollfd pfd;
      pfd.fd = p.parentFd;
      // Yes, backwards, PIPE_IN / PIPE_OUT are defined from the
      // child's point of view.
      pfd.events = (p.direction == PIPE_IN ?  POLLOUT : POLLIN);
      fds.push_back(pfd);
    }

    int r;
    do {
      r = ::poll(fds.data(), fds.size(), -1);
    } while (r == -1 && errno == EINTR);
    checkUnixError(r, "poll");

    for (int i = 0; i < pipes_.size(); ++i) {
      auto& p = pipes_[i];
      DCHECK_EQ(fds[i].fd, p.parentFd);
      short events = fds[i].revents;

      bool closed = false;
      if (events & POLLOUT) {
        DCHECK(!(events & POLLIN));
        if (writeCallback(p.parentFd, p.childFd)) {
          toClose.push_back(i);
          closed = true;
        }
      }

      if (events & POLLIN) {
        DCHECK(!(events & POLLOUT));
        if (readCallback(p.parentFd, p.childFd)) {
          toClose.push_back(i);
          closed = true;
        }
      }

      if ((events & (POLLHUP | POLLERR)) && !closed) {
        toClose.push_back(i);
        closed = true;
      }
    }

    // Close the fds in reverse order so the indexes hold after erase()
    for (int idx : boost::adaptors::reverse(toClose)) {
      auto pos = pipes_.begin() + idx;
      closeChecked(pos->parentFd);
      pipes_.erase(pos);
    }
  }
}

int Subprocess::findByChildFd(int childFd) const {
  auto pos = std::lower_bound(
      pipes_.begin(), pipes_.end(), childFd,
      [] (const PipeInfo& info, int fd) { return info.childFd < fd; });
  if (pos == pipes_.end() || pos->childFd != childFd) {
    throw std::invalid_argument(folly::to<std::string>(
        "child fd not found ", childFd));
  }
  return pos - pipes_.begin();
}

void Subprocess::closeParentFd(int childFd) {
  int idx = findByChildFd(childFd);
  closeChecked(pipes_[idx].parentFd);
  pipes_.erase(pipes_.begin() + idx);
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

}  // namespace

}  // namespace folly

