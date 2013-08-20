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

#include "folly/Subprocess.h"

#include <sys/prctl.h>
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
#include "folly/Exception.h"
#include "folly/FileUtil.h"
#include "folly/ScopeGuard.h"
#include "folly/String.h"
#include "folly/io/Cursor.h"

extern char** environ;

constexpr int kExecFailure = 127;
constexpr int kChildFailure = 126;

namespace folly {

ProcessReturnCode::State ProcessReturnCode::state() const {
  if (rawStatus_ == RV_NOT_STARTED) return NOT_STARTED;
  if (rawStatus_ == RV_RUNNING) return RUNNING;
  if (WIFEXITED(rawStatus_)) return EXITED;
  if (WIFSIGNALED(rawStatus_)) return KILLED;
  throw std::runtime_error(to<std::string>(
      "Invalid ProcessReturnCode: ", rawStatus_));
}

void ProcessReturnCode::enforce(State expected) const {
  State s = state();
  if (s != expected) {
    throw std::logic_error(to<std::string>("Invalid state ", s,
                                           " expected ", expected));
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

SubprocessSpawnError::SubprocessSpawnError(const char* executable,
                                           int errCode,
                                           int errnoValue)
  : errnoValue_(errnoValue),
    what_(to<std::string>(errCode == kExecFailure ?
                            "failed to execute " :
                            "error preparing to execute ",
                          executable, ": ", errnoStr(errnoValue))) {
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
  closeAll();
}

namespace {
void closeChecked(int fd) {
  checkUnixError(::close(fd), "close");
}

struct ChildErrorInfo {
  int errCode;
  int errnoValue;
};

void childError(int errFd, int errCode, int errnoValue) FOLLY_NORETURN;
void childError(int errFd, int errCode, int errnoValue) {
  ChildErrorInfo info = {errCode, errnoValue};
  // Write the error information over the pipe to our parent process.
  // We can't really do anything else if this write call fails.
  writeNoInt(errFd, &info, sizeof(info));
  // exit
  _exit(errCode);
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

  // On error, close all of the pipes_
  auto pipesGuard = makeGuard([&] {
    for (auto& p : this->pipes_) {
      CHECK_ERR(::close(p.parentFd));
    }
  });

  // Create a pipe to use to receive error information from the child,
  // in case it fails before calling exec()
  int errFds[2];
  int r = ::pipe(errFds);
  checkUnixError(r, "pipe");
  SCOPE_EXIT {
    CHECK_ERR(::close(errFds[0]));
    if (errFds[1] >= 0) {
      CHECK_ERR(::close(errFds[1]));
    }
  };
  // Ask the child to close the read end of the error pipe.
  options.fdActions_[errFds[0]] = CLOSE;
  // Set the close-on-exec flag on the write side of the pipe.
  // This way the pipe will be closed automatically in the child if execve()
  // succeeds.  If the exec fails the child can write error information to the
  // pipe.
  r = fcntl(errFds[1], F_SETFD, FD_CLOEXEC);
  checkUnixError(r, "set FD_CLOEXEC");

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
  CHECK_ERR(::close(errFds[1]));
  errFds[1] = -1;

  // Read from the errFd pipe, to tell if the child ran into any errors before
  // calling exec()
  readChildErrorPipe(errFds[0], executable);

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
      CHECK_ERR(::close(cfd));
    }
  };

  int r;
  for (auto& p : options.fdActions_) {
    if (p.second == PIPE_IN || p.second == PIPE_OUT) {
      int fds[2];
      r = ::pipe(fds);
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
  r = ::sigfillset(&allBlocked);
  checkUnixError(r, "sigfillset");
  sigset_t oldSignals;

  r = pthread_sigmask(SIG_SETMASK, &allBlocked, &oldSignals);
  checkPosixError(r, "pthread_sigmask");
  SCOPE_EXIT {
    // Restore signal mask
    r = pthread_sigmask(SIG_SETMASK, &oldSignals, nullptr);
    CHECK_EQ(r, 0) << "pthread_sigmask: " << errnoStr(r);  // shouldn't fail
  };

  pid_t pid = vfork();
  if (pid == 0) {
    int errnoValue = prepareChild(options, &oldSignals);
    if (errnoValue != 0) {
      childError(errFd, kChildFailure, errnoValue);
    }

    errnoValue = runChild(executable, argVec, envVec, options);
    // If we get here, exec() failed.
    childError(errFd, kExecFailure, errnoValue);
  }
  // In parent.  Make sure vfork() succeeded.
  checkUnixError(pid, errno, "vfork");

  // Child is alive.  We have to be very careful about throwing after this
  // point.  We are inside the constructor, so if we throw the Subprocess
  // object will have never existed, and the destructor will never be called.
  //
  // We should only throw if we got an error via the errFd, and we know the
  // child has exited and can be immediately waited for.  In all other cases,
  // we have no way of cleaning up the child.
  pid_ = pid;
  returnCode_ = ProcessReturnCode(RV_RUNNING);
}

int Subprocess::prepareChild(const Options& options,
                             const sigset_t* sigmask) const {
  // While all signals are blocked, we must reset their
  // dispositions to default.
  for (int sig = 1; sig < NSIG; ++sig) {
    ::signal(sig, SIG_DFL);
  }
  // Unblock signals; restore signal mask.
  int r = pthread_sigmask(SIG_SETMASK, sigmask, nullptr);
  if (r != 0) {
    return r;  // pthread_sigmask() returns an errno value
  }

  // Close parent's ends of all pipes
  for (auto& p : pipes_) {
    r = ::close(p.parentFd);
    if (r == -1) {
      return errno;
    }
  }

  // Close all fds that we're supposed to close.
  // Note that we're ignoring errors here, in case some of these
  // fds were set to close on exec.
  for (auto& p : options.fdActions_) {
    if (p.second == CLOSE) {
      ::close(p.first);
    } else {
      r = ::dup2(p.second, p.first);
      if (r == -1) {
        return errno;
      }
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

  // Opt to receive signal on parent death, if requested
  if (options.parentDeathSignal_ != 0) {
    r = prctl(PR_SET_PDEATHSIG, options.parentDeathSignal_, 0, 0, 0);
    if (r == -1) {
      return errno;
    }
  }

  return 0;
}

int Subprocess::runChild(const char* executable,
                         char** argv, char** env,
                         const Options& options) const {
  // Now, finally, exec.
  int r;
  if (options.usePath_) {
    ::execvp(executable, argv);
  } else {
    ::execve(executable, argv, env);
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
    LOG(ERROR) << "unexpected error trying to read from child error pipe " <<
      "rc=" << rc << ", errno=" << errno;
    return;
  }

  // We got error data from the child.  The child should exit immediately in
  // this case, so wait on it to clean up.
  wait();

  // Throw to signal the error
  throw SubprocessSpawnError(executable, info.errCode, info.errnoValue);
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
  pid_ = -1;
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

pid_t Subprocess::pid() const {
  return pid_;
}

namespace {

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
    StringPiece input) {
  IOBufQueue inputQueue;
  inputQueue.wrapBuffer(input.data(), input.size());

  auto outQueues = communicateIOBuf(std::move(inputQueue));
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
    IOBufQueue input) {
  // If the user supplied a non-empty input buffer, make sure
  // that stdin is a pipe so we can write the data.
  if (!input.empty()) {
    // findByChildFd() will throw std::invalid_argument if no pipe for
    // STDIN_FILENO exists
    findByChildFd(STDIN_FILENO);
  }

  std::pair<IOBufQueue, IOBufQueue> out;

  auto readCallback = [&] (int pfd, int cfd) -> bool {
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

  auto writeCallback = [&] (int pfd, int cfd) -> bool {
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

