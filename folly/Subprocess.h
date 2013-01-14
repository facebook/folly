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

/**
 * Subprocess library, modeled after Python's subprocess module
 * (http://docs.python.org/2/library/subprocess.html)
 *
 * This library defines one class (Subprocess) which represents a child
 * process.  Subprocess has two constructors: one that takes a vector<string>
 * and executes the given executable without using the shell, and one
 * that takes a string and executes the given command using the shell.
 * Subprocess allows you to redirect the child's standard input, standard
 * output, and standard error to/from child descriptors in the parent,
 * or to create communication pipes between the child and the parent.
 *
 * The simplest example is a thread-safe version of the system() library
 * function:
 *    Subprocess(cmd).wait();
 * which executes the command using the default shell and waits for it
 * to complete, returning the exit status.
 *
 * A thread-safe version of popen() (type="r", to read from the child):
 *    Subprocess proc(cmd, Subprocess::pipeStdout());
 *    // read from proc.stdout()
 *    proc.wait();
 *
 * A thread-safe version of popen() (type="w", to write from the child):
 *    Subprocess proc(cmd, Subprocess::pipeStdin());
 *    // write to proc.stdin()
 *    proc.wait();
 *
 * If you want to redirect both stdin and stdout to pipes, you can, but
 * note that you're subject to a variety of deadlocks.  You'll want to use
 * nonblocking I/O; look at the implementation of communicate() for an example.
 *
 * communicate() is a way to communicate to a child via its standard input,
 * standard output, and standard error.  It buffers everything in memory,
 * so it's not great for large amounts of data (or long-running processes),
 * but it insulates you from the deadlocks mentioned above.
 */
#ifndef FOLLY_SUBPROCESS_H_
#define FOLLY_SUBPROCESS_H_

#include <sys/types.h>
#include <signal.h>
#include <wait.h>

#include <exception>
#include <vector>
#include <string>

#include <boost/container/flat_map.hpp>
#include <boost/operators.hpp>
#include <boost/noncopyable.hpp>

#include "folly/io/IOBufQueue.h"
#include "folly/MapUtil.h"
#include "folly/Portability.h"
#include "folly/Range.h"

namespace folly {

/**
 * Class to wrap a process return code.
 */
class Subprocess;
class ProcessReturnCode {
  friend class Subprocess;
 public:
  enum State {
    NOT_STARTED,
    RUNNING,
    EXITED,
    KILLED
  };

  /**
   * Process state.  One of:
   * NOT_STARTED: process hasn't been started successfully
   * RUNNING: process is currently running
   * EXITED: process exited (successfully or not)
   * KILLED: process was killed by a signal.
   */
  State state() const;

  /**
   * Helper wrappers around state().
   */
  bool notStarted() const { return state() == NOT_STARTED; }
  bool running() const { return state() == RUNNING; }
  bool exited() const { return state() == EXITED; }
  bool killed() const { return state() == KILLED; }

  /**
   * Exit status.  Only valid if state() == EXITED; throws otherwise.
   */
  int exitStatus() const;

  /**
   * Signal that caused the process's termination.  Only valid if
   * state() == KILLED; throws otherwise.
   */
  int killSignal() const;

  /**
   * Was a core file generated?  Only valid if state() == KILLED; throws
   * otherwise.
   */
  bool coreDumped() const;

  /**
   * String representation; one of
   * "not started"
   * "running"
   * "exited with status <status>"
   * "killed by signal <signal>"
   * "killed by signal <signal> (core dumped)"
   */
  std::string str() const;

  /**
   * Helper function to enforce a precondition based on this.
   * Throws std::logic_error if in an unexpected state.
   */
  void enforce(State state) const;
 private:
  explicit ProcessReturnCode(int rv) : rawStatus_(rv) { }
  static constexpr int RV_NOT_STARTED = -2;
  static constexpr int RV_RUNNING = -1;

  int rawStatus_;
};

/**
 * Exception thrown by *Checked methods of Subprocess.
 */
class CalledProcessError : public std::exception {
 public:
  explicit CalledProcessError(ProcessReturnCode rc);
  ~CalledProcessError() throw() { }
  const char* what() const throw() FOLLY_OVERRIDE { return what_.c_str(); }
  ProcessReturnCode returnCode() const { return returnCode_; }
 private:
  ProcessReturnCode returnCode_;
  std::string what_;
};

/**
 * Subprocess.
 */
class Subprocess : private boost::noncopyable {
 public:
  static const int CLOSE = -1;
  static const int PIPE = -2;
  static const int PIPE_IN = -3;
  static const int PIPE_OUT = -4;

  /**
   * Class representing various options: file descriptor behavior, and
   * whether to use $PATH for searching for the executable,
   *
   * By default, we don't use $PATH, file descriptors are closed if
   * the close-on-exec flag is set (fcntl FD_CLOEXEC) and inherited
   * otherwise.
   */
  class Options : private boost::orable<Options> {
    friend class Subprocess;
   public:
    Options() : closeOtherFds_(false), usePath_(false) { }

    /**
     * Change action for file descriptor fd.
     *
     * "action" may be another file descriptor number (dup2()ed before the
     * child execs), or one of CLOSE, PIPE_IN, and PIPE_OUT.
     *
     * CLOSE: close the file descriptor in the child
     * PIPE_IN: open a pipe *from* the child
     * PIPE_OUT: open a pipe *to* the child
     *
     * PIPE is a shortcut; same as PIPE_IN for stdin (fd 0), same as
     * PIPE_OUT for stdout (fd 1) or stderr (fd 2), and an error for
     * other file descriptors.
     */
    Options& fd(int fd, int action);

    /**
     * Shortcut to change the action for standard input.
     */
    Options& stdin(int action) { return fd(0, action); }

    /**
     * Shortcut to change the action for standard output.
     */
    Options& stdout(int action) { return fd(1, action); }

    /**
     * Shortcut to change the action for standard error.
     * Note that stderr(1) will redirect the standard error to the same
     * file descriptor as standard output; the equivalent of bash's "2>&1"
     */
    Options& stderr(int action) { return fd(2, action); }

    /**
     * Close all other fds (other than standard input, output, error,
     * and file descriptors explicitly specified with fd()).
     *
     * This is potentially slow; it's generally a better idea to
     * set the close-on-exec flag on all file descriptors that shouldn't
     * be inherited by the child.
     *
     * Even with this option set, standard input, output, and error are
     * not closed; use stdin(CLOSE), stdout(CLOSE), stderr(CLOSE) if you
     * desire this.
     */
    Options& closeOtherFds() { closeOtherFds_ = true; return *this; }

    /**
     * Use the search path ($PATH) when searching for the executable.
     */
    Options& usePath() { usePath_ = true; return *this; }

    /**
     * Helpful way to combine Options.
     */
    Options& operator|=(const Options& other);

   private:
    typedef boost::container::flat_map<int, int> FdMap;
    FdMap fdActions_;
    bool closeOtherFds_;
    bool usePath_;
  };

  static Options pipeStdin() { return Options().stdin(PIPE); }
  static Options pipeStdout() { return Options().stdout(PIPE); }
  static Options pipeStderr() { return Options().stderr(PIPE); }

  /**
   * Create a subprocess from the given arguments.  argv[0] must be listed.
   * If not-null, executable must be the actual executable
   * being used (otherwise it's the same as argv[0]).
   *
   * If env is not-null, it must contain name=value strings to be used
   * as the child's environment; otherwise, we inherit the environment
   * from the parent.  env must be null if options.usePath is set.
   */
  explicit Subprocess(
      const std::vector<std::string>& argv,
      const Options& options = Options(),
      const char* executable = nullptr,
      const std::vector<std::string>* env = nullptr);
  ~Subprocess();

  /**
   * Create a subprocess run as a shell command (as shell -c 'command')
   *
   * The shell to use is taken from the environment variable $SHELL,
   * or /bin/sh if $SHELL is unset.
   */
  explicit Subprocess(
      const std::string& cmd,
      const Options& options = Options(),
      const std::vector<std::string>* env = nullptr);

  /**
   * Append all data, close the stdin (to-child) fd, and read all data,
   * except that this is done in a safe manner to prevent deadlocking.
   *
   * If writeStdin() is given in flags, the process must have been opened with
   * stdinFd=PIPE.
   *
   * If readStdout() is given in flags, the first returned value will be the
   * value read from the child's stdout; the child must have been opened with
   * stdoutFd=PIPE.
   *
   * If readStderr() is given in flags, the second returned value will be the
   * value read from the child's stderr; the child must have been opened with
   * stderrFd=PIPE.
   *
   * Note that communicate() returns when all pipes to/from the child are
   * closed; the child might stay alive after that, so you must still wait().
   *
   * communicateIOBuf uses IOBufQueue for buffering (which has the advantage
   * that it won't try to allocate all data at once).  communicate
   * uses strings for simplicity.
   */
  class CommunicateFlags : private boost::orable<CommunicateFlags> {
    friend class Subprocess;
   public:
    CommunicateFlags()
      : writeStdin_(false), readStdout_(false), readStderr_(false) { }
    CommunicateFlags& writeStdin() { writeStdin_ = true; return *this; }
    CommunicateFlags& readStdout() { readStdout_ = true; return *this; }
    CommunicateFlags& readStderr() { readStderr_ = true; return *this; }

    CommunicateFlags& operator|=(const CommunicateFlags& other);
   private:
    bool writeStdin_;
    bool readStdout_;
    bool readStderr_;
  };

  static CommunicateFlags writeStdin() {
    return CommunicateFlags().writeStdin();
  }
  static CommunicateFlags readStdout() {
    return CommunicateFlags().readStdout();
  }
  static CommunicateFlags readStderr() {
    return CommunicateFlags().readStderr();
  }

  std::pair<IOBufQueue, IOBufQueue> communicateIOBuf(
      const CommunicateFlags& flags = readStdout(),
      IOBufQueue data = IOBufQueue());

  std::pair<std::string, std::string> communicate(
      const CommunicateFlags& flags = readStdout(),
      StringPiece data = StringPiece());

  /**
   * Communicate with the child until all pipes to/from the child are closed.
   *
   * readCallback(pfd, cfd) will be called whenever there's data available
   * on any pipe *from* the child (PIPE_OUT).  pfd is the file descriptor
   * in the parent (that you use to read from); cfd is the file descriptor
   * in the child (used for identifying the stream; 1 = child's standard
   * output, 2 = child's standard error, etc)
   *
   * writeCallback(pfd, cfd) will be called whenever a pipe *to* the child is
   * writable (PIPE_IN).  pfd is the file descriptor in the parent (that you
   * use to write to); cfd is the file descriptor in the child (used for
   * identifying the stream; 0 = child's standard input, etc)
   *
   * The read and write callbacks must read from / write to pfd and return
   * false during normal operation or true at end-of-file;
   * communicate() will then close the pipe.  Note that pfd is
   * nonblocking, so be prepared for read() / write() to return -1 and
   * set errno to EAGAIN (in which case you should return false).
   *
   * NOTE that you MUST consume all data passed to readCallback (or return
   * true, which will close the pipe, possibly sending SIGPIPE to the child or
   * making its writes fail with EPIPE), and you MUST write to a writable pipe
   * (or return true, which will close the pipe).  To do otherwise is an
   * error.  You must do this even for pipes you are not interested in.
   *
   * Note that communicate() returns when all pipes to/from the child are
   * closed; the child might stay alive after that, so you must still wait().
   *
   * Most users won't need to use this; the simpler version of communicate
   * (which buffers data in memory) will probably work fine.
   */
  typedef std::function<bool(int, int)> FdCallback;
  void communicate(FdCallback readCallback, FdCallback writeCallback);

  /**
   * Return the child's pid, or -1 if the child wasn't successfully spawned
   * or has already been wait()ed upon.
   */
  pid_t pid() const;

  /**
   * Return the child's status (as per wait()) if the process has already
   * been waited on, -1 if the process is still running, or -2 if the process
   * hasn't been successfully started.  NOTE that this does not poll, but
   * returns the status stored in the Subprocess object.
   */
  ProcessReturnCode returnCode() const { return returnCode_; }

  /**
   * Poll the child's status and return it, return -1 if the process
   * is still running.  NOTE that it is illegal to call poll again after
   * poll indicated that the process has terminated, or to call poll on a
   * process that hasn't been successfully started (the constructor threw an
   * exception).
   */
  ProcessReturnCode poll();

  /**
   * Poll the child's status.  If the process is still running, return false.
   * Otherwise, return true if the process exited with status 0 (success),
   * or throw CalledProcessError if the process exited with a non-zero status.
   */
  bool pollChecked();

  /**
   * Wait for the process to terminate and return its status.
   * Similarly to poll, it is illegal to call wait after the process
   * has already been reaped or if the process has not successfully started.
   */
  ProcessReturnCode wait();

  /**
   * Wait for the process to terminate, throw if unsuccessful.
   */
  void waitChecked();

  /**
   * Set all pipes from / to child non-blocking.  communicate() does
   * this for you.
   */
  void setAllNonBlocking();

  /**
   * Get parent file descriptor corresponding to the given file descriptor
   * in the child.  Throws if childFd isn't a pipe (PIPE_IN / PIPE_OUT).
   * Do not close() the return file descriptor; use closeParentFd, below.
   */
  int parentFd(int childFd) const {
    return pipes_[findByChildFd(childFd)].parentFd;
  }
  int stdin() const { return parentFd(0); }
  int stdout() const { return parentFd(1); }
  int stderr() const { return parentFd(2); }

  /**
   * Close the parent file descriptor given a file descriptor in the child.
   */
  void closeParentFd(int childFd);

  /**
   * Send a signal to the child.  Shortcuts for the commonly used Unix
   * signals are below.
   */
  void sendSignal(int signal);
  void terminate() { sendSignal(SIGTERM); }
  void kill() { sendSignal(SIGKILL); }

 private:
  static const int RV_RUNNING = ProcessReturnCode::RV_RUNNING;
  static const int RV_NOT_STARTED = ProcessReturnCode::RV_NOT_STARTED;

  void spawn(
      std::unique_ptr<const char*[]> argv,
      const char* executable,
      const Options& options,
      const std::vector<std::string>* env);

  // Action to run in child.
  // Note that this runs after vfork(), so tread lightly.
  void runChild(const char* executable, char** argv, char** env,
                const Options& options) const;

  /**
   * Close all file descriptors.
   */
  void closeAll();

  // return index in pipes_
  int findByChildFd(int childFd) const;

  pid_t pid_;
  ProcessReturnCode returnCode_;

  // The number of pipes between parent and child is assumed to be small,
  // so we're happy with a vector here, even if it means linear erase.
  // sorted by childFd
  struct PipeInfo : private boost::totally_ordered<PipeInfo> {
    int parentFd;
    int childFd;
    int direction;  // one of PIPE_IN / PIPE_OUT
    bool operator<(const PipeInfo& other) const {
      return childFd < other.childFd;
    }
    bool operator==(const PipeInfo& other) const {
      return childFd == other.childFd;
    }
  };
  std::vector<PipeInfo> pipes_;
};

inline Subprocess::Options& Subprocess::Options::operator|=(
    const Subprocess::Options& other) {
  if (this == &other) return *this;
  // Replace
  for (auto& p : other.fdActions_) {
    fdActions_[p.first] = p.second;
  }
  closeOtherFds_ |= other.closeOtherFds_;
  usePath_ |= other.usePath_;
  return *this;
}

inline Subprocess::CommunicateFlags& Subprocess::CommunicateFlags::operator|=(
    const Subprocess::CommunicateFlags& other) {
  if (this == &other) return *this;
  writeStdin_ |= other.writeStdin_;
  readStdout_ |= other.readStdout_;
  readStderr_ |= other.readStderr_;
  return *this;
}

}  // namespace folly

#endif /* FOLLY_SUBPROCESS_H_ */

