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

#include <folly/Subprocess.h>

#include <sys/types.h>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <boost/container/flat_set.hpp>
#include <glog/logging.h>

#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/String.h>
#include <folly/container/span.h>
#include <folly/experimental/io/FsUtil.h>
#include <folly/gen/Base.h>
#include <folly/gen/File.h>
#include <folly/gen/String.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/portability/SysSyscall.h>
#include <folly/portability/Unistd.h>
#include <folly/testing/TestUtil.h>

FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")

using namespace folly;
using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

namespace std::chrono {
template <typename Rep, typename Period>
void PrintTo(std::chrono::duration<Rep, Period> duration, std::ostream* out) {
  const auto ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
  const auto ms_float = ns.count() / 1000000.0;
  *out << ms_float << "ms";
}
} // namespace std::chrono

namespace {
sigset_t makeSignalMask(folly::span<int const> signals) {
  sigset_t sigmask;
  sigemptyset(&sigmask);
  for (auto sig : signals) {
    sigaddset(&sigmask, sig);
  }
  return sigmask;
}

struct ScopedSignalMaskOverride {
  sigset_t sigmask;
  explicit ScopedSignalMaskOverride(folly::span<int const> signals) {
    auto target = makeSignalMask(signals);
    PCHECK(0 == pthread_sigmask(SIG_SETMASK, &target, &sigmask));
  }
  ~ScopedSignalMaskOverride() {
    PCHECK(0 == pthread_sigmask(SIG_SETMASK, &sigmask, nullptr));
  }
};

uint64_t readSignalMask(sigset_t sigmask) {
  static_assert(NSIG - 1 <= 64); // 0 is not a signal
  uint64_t ret = 0;
  for (int sig = 1; sig < NSIG; ++sig) {
    if (sigismember(&sigmask, sig)) {
      ret |= (uint64_t(1) << (sig - 1));
    }
  }
  return ret;
}

sigset_t getCurrentSignalMask() {
  sigset_t sigmask;
  pthread_sigmask(SIG_SETMASK, nullptr, &sigmask);
  return sigmask;
}

std::string_view readOneLineOfProcSelfStatus(
    std::string_view text, std::string_view key) {
  std::vector<std::string_view> lines;
  folly::split('\n', text, lines);
  auto prefix = fmt::format("{}:", key);
  auto iter = std::find_if(lines.begin(), lines.end(), [&](auto line) {
    return folly::StringPiece(line).starts_with(prefix);
  });
  if (iter == lines.end()) {
    return {};
  }
  auto line = *iter;
  line.remove_prefix(prefix.size());
  while (!line.empty() && std::isspace(line[0])) {
    line.remove_prefix(1);
  }
  return line;
}

} // namespace

struct SubprocessFdActionsListTest : testing::Test {};

TEST_F(SubprocessFdActionsListTest, stress) {
  std::mt19937 rng;
  std::uniform_int_distribution<size_t> dist{0, 255};
  for (size_t sz = 0; sz < 128; ++sz) {
    std::map<int, int> map;
    for (size_t i = 0; i < sz; ++i) {
      while (true) {
        auto n = dist(rng);
        if (map.contains(n)) {
          continue;
        }
        map[int(n)] = -int(n);
        break;
      }
    }
    std::vector<std::pair<int, int>> vec{map.begin(), map.end()};
    detail::SubprocessFdActionsList list{vec};
    for (size_t fd = 0; fd < 256; ++fd) {
      auto found = list.find(fd);
      EXPECT_EQ(map.contains(fd), found != nullptr);
      if (found) {
        EXPECT_EQ(-int(fd), *found);
      }
    }
  }
}

TEST(SimpleSubprocessTest, ExitsSuccessfully) {
  Subprocess proc(std::vector<std::string>{"/bin/true"});
  EXPECT_EQ(0, proc.wait().exitStatus());
}

TEST(SimpleSubprocessTest, ExitsSuccessfullyChecked) {
  Subprocess proc(std::vector<std::string>{"/bin/true"});
  proc.waitChecked();
}

TEST(SimpleSubprocessTest, ExitsWithError) {
  Subprocess proc(std::vector<std::string>{"/bin/false"});
  EXPECT_EQ(1, proc.wait().exitStatus());
}

TEST(SimpleSubprocessTest, ExitsWithErrorChecked) {
  Subprocess proc(std::vector<std::string>{"/bin/false"});
  EXPECT_THROW(proc.waitChecked(), CalledProcessError);
}

TEST(SimpleSubprocessTest, DefaultConstructibleProcessReturnCode) {
  ProcessReturnCode retcode;
  EXPECT_TRUE(retcode.notStarted());
}

TEST(SimpleSubprocessTest, MoveSubprocess) {
  Subprocess old_proc(std::vector<std::string>{"/bin/true"});
  EXPECT_TRUE(old_proc.returnCode().running());
  auto new_proc = std::move(old_proc);
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_TRUE(old_proc.returnCode().notStarted());
  EXPECT_TRUE(new_proc.returnCode().running());
  EXPECT_EQ(0, new_proc.wait().exitStatus());
  // Now old_proc is destroyed, but we don't crash.
}

TEST(SimpleSubprocessTest, DefaultConstructor) {
  Subprocess proc;
  EXPECT_TRUE(proc.returnCode().notStarted());

  {
    auto p1 = Subprocess(std::vector<std::string>{"/bin/true"});
    proc = std::move(p1);
  }

  EXPECT_TRUE(proc.returnCode().running());
  EXPECT_EQ(0, proc.wait().exitStatus());
}

#define EXPECT_SPAWN_OPT_ERROR(err, errMsg, options, cmd, ...)        \
  do {                                                                \
    try {                                                             \
      Subprocess proc(                                                \
          std::vector<std::string>{(cmd), ##__VA_ARGS__}, (options)); \
      ADD_FAILURE() << "expected an error when running " << (cmd);    \
    } catch (const SubprocessSpawnError& ex) {                        \
      EXPECT_EQ((err), ex.errnoValue());                              \
      if (StringPiece(ex.what()).find(errMsg) == StringPiece::npos) { \
        ADD_FAILURE() << "failed to find \"" << (errMsg)              \
                      << "\" in exception: \"" << ex.what() << "\"";  \
      }                                                               \
    }                                                                 \
  } while (0)

#define EXPECT_SPAWN_ERROR(err, errMsg, cmd, ...) \
  EXPECT_SPAWN_OPT_ERROR(err, errMsg, Subprocess::Options(), cmd, ##__VA_ARGS__)

TEST(SimpleSubprocessTest, ExecFails) {
  EXPECT_SPAWN_ERROR(
      ENOENT, "failed to execute /no/such/file:", "/no/such/file");
  EXPECT_SPAWN_ERROR(EACCES, "failed to execute /etc/passwd:", "/etc/passwd");
  EXPECT_SPAWN_ERROR(
      ENOTDIR,
      "failed to execute /etc/passwd/not/a/file:",
      "/etc/passwd/not/a/file");
}

TEST(SimpleSubprocessTest, ShellExitsSuccesssfully) {
  Subprocess proc("true");
  EXPECT_EQ(0, proc.wait().exitStatus());
}

TEST(SimpleSubprocessTest, ShellExitsWithError) {
  Subprocess proc("false");
  EXPECT_EQ(1, proc.wait().exitStatus());
}

TEST(SimpleSubprocessTest, ChangeChildDirectorySuccessfully) {
  // The filesystem root normally lacks a 'true' binary
  EXPECT_EQ(0, chdir("/"));
  EXPECT_SPAWN_ERROR(ENOENT, "failed to execute ./true", "./true");
  // The child can fix that by moving to /bin before exec().
  Subprocess proc("./true", Subprocess::Options().chdir("/bin"));
  EXPECT_EQ(0, proc.wait().exitStatus());
}

TEST(SimpleSubprocessTest, ChangeChildDirectoryWithError) {
  try {
    Subprocess proc(
        std::vector<std::string>{"/bin/true"},
        Subprocess::Options().chdir("/usually/this/is/not/a/valid/directory/"));
    ADD_FAILURE() << "expected to fail when changing the child's directory";
  } catch (const SubprocessSpawnError& ex) {
    EXPECT_EQ(ENOENT, ex.errnoValue());
    const std::string expectedError =
        "error preparing to execute /bin/true: No such file or directory";
    if (StringPiece(ex.what()).find(expectedError) == StringPiece::npos) {
      ADD_FAILURE() << "failed to find \"" << expectedError
                    << "\" in exception: \"" << ex.what() << "\"";
    }
  }
}

TEST(SimpleSubprocessTest, waitOrTerminateOrKillWaitsIfProcessExits) {
  auto const opts =
      Subprocess::Options()
          .stdinFd(Subprocess::DEV_NULL)
          .stdoutFd(Subprocess::DEV_NULL);
  Subprocess proc(std::vector<std::string>{"/bin/cat"}, opts);
  auto retCode = proc.waitOrTerminateOrKill(1s, 1s);
  EXPECT_TRUE(retCode.exited());
  EXPECT_EQ(0, retCode.exitStatus());
}

TEST(SimpleSubprocessTest, waitOrTerminateOrKillTerminatesIfTimeout) {
  auto const opts =
      Subprocess::Options() //
          .pipeStdin()
          .stdoutFd(Subprocess::DEV_NULL);
  Subprocess proc(std::vector<std::string>{"/bin/cat"}, opts);
  auto retCode = proc.waitOrTerminateOrKill(10ms, 10ms);
  EXPECT_TRUE(retCode.killed());
  EXPECT_EQ(SIGTERM, retCode.killSignal());
}

TEST(
    SimpleSubprocessTest,
    destructor_doesNotFail_ifOkToDestroyWhileProcessRunning) {
  pid_t pid;
  {
    Subprocess proc(
        std::vector<std::string>{"/bin/sleep", "10"},
        Subprocess::Options().allowDestructionWhileProcessRunning(true));
    pid = proc.pid();
  }
  auto proc2 = Subprocess::fromExistingProcess(pid);
  proc2.terminateOrKill(10ms);
}

TEST(SubprocessTest, FatalOnDestroy) {
  auto const opts =
      Subprocess::Options() //
          .pipeStdin()
          .pipeStdout()
          .pipeStderr();
  EXPECT_DEATH(
      Subprocess(std::vector<std::string>{"/bin/cat"}, opts),
      "Subprocess destroyed without reaping child");
}

TEST(SubprocessTest, KillOnDestroy) {
  pid_t pid;
  {
    Subprocess proc(
        std::vector<std::string>{"/bin/sleep", "10"},
        Subprocess::Options().killChildOnDestruction());
    pid = proc.pid();
  }
  // The process should no longer exist
  EXPECT_EQ(-1, kill(pid, 0));
  EXPECT_EQ(ESRCH, errno);
}

#if defined(__linux__)

TEST(SubprocessTest, TerminateOnDestroy) {
  // Enabled only on Linux because this test uses pidfd, which is Linux-only.
  // V.s. attempting to kill() a pid that was already wait()ed to check for an
  // error returned from kill(), which is subject to races on the system.
  auto pidfd = -1;
  pid_t pid;
  std::chrono::steady_clock::time_point start;
  const auto terminateTimeout = 100ms;
  {
    sigset_t mask;
    sigfillset(&mask);
    auto const opts =
        Subprocess::Options() //
            .pipeStdin()
            .pipeStdout()
            .pipeStderr()
            .setSignalMask(mask)
            .terminateChildOnDestruction(terminateTimeout);
    // Spawn a process that ignores SIGTERM
    Subprocess proc(std::vector<std::string>{"/bin/cat"}, opts);
    pid = proc.pid();
    pidfd = syscall(SYS_pidfd_open, pid, /* flags = */ 0);
    PCHECK(-1 != pidfd);
    {
      auto rc = syscall(
          SYS_pidfd_send_signal,
          pidfd,
          SIGTERM,
          /* info = */ nullptr,
          /* flags = */ 0);
      PCHECK(0 == rc);
    }
    start = std::chrono::steady_clock::now();
  }
  const auto end = std::chrono::steady_clock::now();
  // The process should no longer exist.
  {
    auto rc = syscall(
        SYS_pidfd_send_signal,
        pidfd,
        SIGTERM,
        /* info = */ nullptr,
        /* flags = */ 0);
    auto const err = errno;
    PCHECK(-1 == rc);
    EXPECT_EQ(ESRCH, err);
  }
  close(pidfd);
  // It should have taken us roughly terminateTimeout in the destructor
  // to wait for the child to exit after SIGTERM before we gave up and sent
  // SIGKILL.
  const auto destructorDuration = end - start;
  EXPECT_GE(destructorDuration, terminateTimeout);
  EXPECT_LT(destructorDuration, terminateTimeout + 5s);
}

#endif

// This method verifies terminateOrKill shouldn't affect the exit
// status if the process has exited already.
TEST(SimpleSubprocessTest, TerminateAfterProcessExit) {
  Subprocess proc(
      std::vector<std::string>{"/bin/bash", "-c", "echo hello; exit 1"},
      Subprocess::Options().pipeStdout().pipeStderr());
  const auto [stdout, stderr] = proc.communicate();
  EXPECT_EQ("hello\n", stdout);
  auto retCode = proc.terminateOrKill(1s);
  EXPECT_TRUE(retCode.exited());
  EXPECT_EQ(1, retCode.exitStatus());
}

// This method tests that if the subprocess handles SIGTERM faster
// enough, we don't have to use SIGKILL to kill it.
TEST(SimpleSubprocessTest, TerminateWithoutKill) {
  // Start a bash process that would sleep for 60 seconds, and the
  // default signal handler should exit itself upon receiving SIGTERM.
  auto const opts = Subprocess::Options().pipeStdin().pipeStdout().pipeStderr();
  Subprocess proc(std::vector<std::string>{"/bin/cat"}, opts);
  auto retCode = proc.terminateOrKill(1s);
  EXPECT_TRUE(retCode.killed());
  EXPECT_EQ(SIGTERM, retCode.killSignal());
}

TEST(SimpleSubprocessTest, TerminateOrKillZeroTimeout) {
  // Using terminateOrKill() with a 0s timeout should immediately kill the
  // process with SIGKILL without bothering to attempt SIGTERM.
  auto const opts = Subprocess::Options().pipeStdin().pipeStdout().pipeStderr();
  Subprocess proc(std::vector<std::string>{"/bin/cat"}, opts);
  auto retCode = proc.terminateOrKill(0s);
  EXPECT_TRUE(retCode.killed());
  EXPECT_EQ(SIGKILL, retCode.killSignal());
}

// This method tests that if the subprocess ignores SIGTERM, we have
// to use SIGKILL to kill it when calling terminateOrKill.
TEST(SimpleSubprocessTest, KillAfterTerminate) {
  sigset_t mask;
  sigfillset(&mask);
  auto const opts =
      Subprocess::Options() //
          .pipeStdin()
          .pipeStdout()
          .pipeStderr()
          .setSignalMask(mask);
  Subprocess proc(std::vector<std::string>{"/bin/cat"}, opts);
  auto retCode = proc.terminateOrKill(10ms);
  EXPECT_TRUE(retCode.killed());
  EXPECT_EQ(SIGKILL, retCode.killSignal());
}

namespace {
boost::container::flat_set<int> getOpenFds() {
  auto pid = getpid();
  auto dirname = to<std::string>("/proc/", pid, "/fd");

  boost::container::flat_set<int> fds;
  for (fs::directory_iterator it(dirname); it != fs::directory_iterator();
       ++it) {
    int fd = to<int>(it->path().filename().native());
    fds.insert(fd);
  }
  return fds;
}

template <class Runnable>
void checkFdLeak(const Runnable& r) {
  // Get the currently open fds.  Check that they are the same both before and
  // after calling the specified function.  We read the open fds from /proc.
  // (If we wanted to work even on systems that don't have /proc, we could
  // perhaps create and immediately close a socket both before and after
  // running the function, and make sure we got the same fd number both times.)
  auto fdsBefore = getOpenFds();
  r();
  auto fdsAfter = getOpenFds();
  EXPECT_EQ(fdsAfter.size(), fdsBefore.size());
}
} // namespace

// Make sure Subprocess doesn't leak any file descriptors
TEST(SimpleSubprocessTest, FdLeakTest) {
  // Normal execution
  checkFdLeak([] {
    Subprocess proc("true");
    EXPECT_EQ(0, proc.wait().exitStatus());
  });
  // Normal execution with pipes
  checkFdLeak([] {
    Subprocess proc(
        "echo foo; echo bar >&2",
        Subprocess::Options().pipeStdout().pipeStderr());
    auto p = proc.communicate();
    EXPECT_EQ("foo\n", p.first);
    EXPECT_EQ("bar\n", p.second);
    proc.waitChecked();
  });

  // Test where the exec call fails()
  checkFdLeak([] {
    EXPECT_SPAWN_ERROR(ENOENT, "failed to execute", "/no/such/file");
  });
  // Test where the exec call fails() with pipes
  checkFdLeak([] {
    try {
      Subprocess proc(
          std::vector<std::string>({"/no/such/file"}),
          Subprocess::Options().pipeStdout().stderrFd(Subprocess::PIPE));
      ADD_FAILURE() << "expected an error when running /no/such/file";
    } catch (const SubprocessSpawnError& ex) {
      EXPECT_EQ(ENOENT, ex.errnoValue());
    }
  });
}

TEST(SimpleSubprocessTest, Detach) {
  auto start = std::chrono::steady_clock::now();
  {
    Subprocess proc(
        std::vector<std::string>{"/bin/sleep", "10"},
        Subprocess::Options().detach());
    EXPECT_EQ(-1, proc.pid());
  }
  auto end = std::chrono::steady_clock::now();
  // We should be able to create and destroy the Subprocess object quickly,
  // without waiting for the sleep process to finish.  This should usually
  // happen in a matter of milliseconds, but we allow up to 5 seconds just to
  // provide lots of leeway on heavily loaded continuous build machines.
  EXPECT_LE(end - start, 5s);
}

TEST(SimpleSubprocessTest, DetachExecFails) {
  // Errors executing the process should be propagated from the grandchild
  // process back to the original parent process.
  EXPECT_SPAWN_OPT_ERROR(
      ENOENT,
      "failed to execute /no/such/file:",
      Subprocess::Options().detach(),
      "/no/such/file");
}

#ifdef __linux__

TEST(SimpleSubprocessTest, AffinitySuccess) {
  cpu_set_t cpuSet0;
  CPU_ZERO(&cpuSet0);
  CPU_SET(1, &cpuSet0);
  CPU_SET(2, &cpuSet0);
  CPU_SET(3, &cpuSet0);
  auto options = Subprocess::Options().pipeStdin().pipeStdout();
  options.setCpuSet(cpuSet0);
  Subprocess proc(std::vector<std::string>{"/bin/cat"}, options);
  EXPECT_NE(proc.pid(), -1);
  cpu_set_t cpuSet1;
  CPU_ZERO(&cpuSet1);
  auto ret = ::sched_getaffinity(proc.pid(), sizeof(cpu_set_t), &cpuSet1);
  CHECK_EQ(ret, 0);
  CHECK_EQ(::memcmp(&cpuSet0, &cpuSet1, sizeof(cpu_set_t)), 0);
  proc.communicate();
  proc.wait();
}

TEST(SimpleSubprocessTest, AffinityFailure) {
  cpu_set_t cpuSet0;
  CPU_ZERO(&cpuSet0);
  CPU_SET(16 * sysconf(_SC_NPROCESSORS_ONLN), &cpuSet0);
  auto options = Subprocess::Options().pipeStdin().pipeStdout();
  options.setCpuSet(cpuSet0);
  EXPECT_THROW(
      Subprocess(std::vector<std::string>{"/bin/cat"}, options),
      SubprocessSpawnError);
}

TEST(SimpleSubprocessTest, AffinityFailureIntoErrnum) {
  cpu_set_t cpuSet0;
  CPU_ZERO(&cpuSet0);
  CPU_SET(16 * sysconf(_SC_NPROCESSORS_ONLN), &cpuSet0);
  auto options = Subprocess::Options().pipeStdin().pipeStdout();
  int cpusetErrnum = 0;
  options.setCpuSet(cpuSet0, to_shared_ptr_non_owning(&cpusetErrnum));
  Subprocess proc(std::vector<std::string>{"/bin/cat"}, options);
  EXPECT_NE(proc.pid(), -1);
  EXPECT_EQ(EINVAL, cpusetErrnum);
  proc.communicate();
  proc.wait();
}

#endif // __linux__

TEST(SimpleSubprocessTest, FromExistingProcess) {
  // Manually fork a child process using fork() without exec(), and test waiting
  // for it using the Subprocess API in the parent process.
  static int constexpr kReturnCode = 123;

  auto pid = fork();
  ASSERT_NE(pid, -1) << "fork failed";
  if (pid == 0) {
    // child process
    _exit(kReturnCode);
  }

  auto child = Subprocess::fromExistingProcess(pid);
  EXPECT_TRUE(child.returnCode().running());
  auto retCode = child.wait();
  EXPECT_TRUE(retCode.exited());
  EXPECT_EQ(kReturnCode, retCode.exitStatus());
}

#ifdef __linux__

TEST(ParentDeathSubprocessTest, ParentDeathSignal) {
  auto helper = folly::test::find_resource(
      "folly/test/subprocess_test_parent_death_helper");
  fs::path tempFile(fs::temp_directory_path() / fs::unique_path());
  std::vector<std::string> args{helper.string(), tempFile.string()};
  Subprocess proc(args);
  // The helper gets killed by its child, see details in
  // SubprocessTestParentDeathHelper.cpp
  ASSERT_EQ(SIGKILL, proc.wait().killSignal());

  // Now wait for the file to be created, see details in
  // SubprocessTestParentDeathHelper.cpp
  while (!fs::exists(tempFile)) {
    usleep(20000); // 20ms
  }

  fs::remove(tempFile);
}

#endif

TEST(PopenSubprocessTest, PopenRead) {
  Subprocess proc("ls /", Subprocess::Options().pipeStdout());
  int found = 0;
  gen::byLine(File(proc.stdoutFd())) | [&](StringPiece line) {
    if (line == "etc" || line == "bin" || line == "usr") {
      ++found;
    }
  };
  EXPECT_EQ(3, found);
  proc.waitChecked();
}

TEST(CommunicateSubprocessTest, SimpleRead) {
  Subprocess proc(
      std::vector<std::string>{"/bin/echo", "-n", "foo", "bar"},
      Subprocess::Options().pipeStdout());
  auto p = proc.communicate();
  EXPECT_EQ("foo bar", p.first);
  proc.waitChecked();
}

TEST(CommunicateSubprocessTest, BigWrite) {
  const int numLines = 1 << 20;
  std::string line("hello\n");
  std::string data;
  data.reserve(numLines * line.size());
  for (int i = 0; i < numLines; ++i) {
    data.append(line);
  }

  Subprocess proc("wc -l", Subprocess::Options().pipeStdin().pipeStdout());
  auto p = proc.communicate(data);
  EXPECT_EQ(folly::format("{}\n", numLines).str(), p.first);
  proc.waitChecked();
}

TEST(CommunicateSubprocessTest, Duplex) {
  // Take 10MB of data and pass them through a filter.
  // One line, as tr is line-buffered
  const int bytes = 10 << 20;
  std::string line(bytes, 'x');

  Subprocess proc("tr a-z A-Z", Subprocess::Options().pipeStdin().pipeStdout());
  auto p = proc.communicate(line);
  EXPECT_EQ(bytes, p.first.size());
  EXPECT_EQ(std::string::npos, p.first.find_first_not_of('X'));
  proc.waitChecked();
}

TEST(CommunicateSubprocessTest, ProcessGroupLeader) {
  const auto testIsLeader = "test $(cut -d ' ' -f 5 /proc/$$/stat) = $$";
  Subprocess nonLeader(testIsLeader);
  EXPECT_THROW(nonLeader.waitChecked(), CalledProcessError);
  Subprocess leader(testIsLeader, Subprocess::Options().processGroupLeader());
  leader.waitChecked();
}

TEST(CommunicateSubprocessTest, Duplex2) {
  checkFdLeak([] {
    // Pipe 200,000 lines through sed
    const size_t numCopies = 100000;
    auto iobuf = IOBuf::copyBuffer("this is a test\nanother line\n");
    IOBufQueue input;
    for (size_t n = 0; n < numCopies; ++n) {
      input.append(iobuf->clone());
    }

    std::vector<std::string> cmd({
        "sed",
        "-u",
        "-e",
        "s/a test/a successful test/",
        "-e",
        "/^another line/w/dev/stderr",
    });
    auto options =
        Subprocess::Options().pipeStdin().pipeStdout().pipeStderr().usePath();
    Subprocess proc(cmd, options);
    auto out = proc.communicateIOBuf(std::move(input));
    proc.waitChecked();

    // Convert stdout and stderr to strings so we can call split() on them.
    fbstring stdoutStr;
    if (out.first.front()) {
      stdoutStr = out.first.move()->moveToFbString();
    }
    fbstring stderrStr;
    if (out.second.front()) {
      stderrStr = out.second.move()->moveToFbString();
    }

    // stdout should be a copy of stdin, with "a test" replaced by
    // "a successful test"
    std::vector<StringPiece> stdoutLines;
    split('\n', stdoutStr, stdoutLines);
    EXPECT_EQ(numCopies * 2 + 1, stdoutLines.size());
    // Strip off the trailing empty line
    if (!stdoutLines.empty()) {
      EXPECT_EQ("", stdoutLines.back());
      stdoutLines.pop_back();
    }
    size_t linenum = 0;
    for (const auto& line : stdoutLines) {
      if ((linenum & 1) == 0) {
        EXPECT_EQ("this is a successful test", line);
      } else {
        EXPECT_EQ("another line", line);
      }
      ++linenum;
    }

    // stderr should only contain the lines containing "another line"
    std::vector<StringPiece> stderrLines;
    split('\n', stderrStr, stderrLines);
    EXPECT_EQ(numCopies + 1, stderrLines.size());
    // Strip off the trailing empty line
    if (!stderrLines.empty()) {
      EXPECT_EQ("", stderrLines.back());
      stderrLines.pop_back();
    }
    for (const auto& line : stderrLines) {
      EXPECT_EQ("another line", line);
    }
  });
}

namespace {

bool readToString(int fd, std::string& buf, size_t maxSize) {
  buf.resize(maxSize);
  char* dest = &buf.front();
  size_t remaining = maxSize;

  ssize_t n = -1;
  while (remaining) {
    n = fileops::read(fd, dest, remaining);
    if (n == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN) {
        break;
      }
      PCHECK("read failed");
    } else if (n == 0) {
      break;
    }
    dest += n;
    remaining -= n;
  }

  buf.resize(dest - buf.data());
  return (n == 0);
}

} // namespace

TEST(CommunicateSubprocessTest, Chatty) {
  checkFdLeak([] {
    const int lineCount = 1000;

    int wcount = 0;
    int rcount = 0;

    auto options =
        Subprocess::Options().pipeStdin().pipeStdout().pipeStderr().usePath();
    std::vector<std::string> cmd{
        "sed",
        "-u",
        "-e",
        "s/a test/a successful test/",
    };

    Subprocess proc(cmd, options);

    auto writeCallback = [&](int pfd, int cfd) -> bool {
      EXPECT_EQ(0, cfd); // child stdin
      EXPECT_EQ(rcount, wcount); // chatty, one read for every write

      auto msg = folly::to<std::string>("a test ", wcount, "\n");

      // Not entirely kosher, we should handle partial writes, but this is
      // fine for writes <= PIPE_BUF
      EXPECT_EQ(msg.size(), writeFull(pfd, msg.data(), msg.size()));

      ++wcount;
      proc.enableNotifications(0, false);

      return (wcount == lineCount);
    };

    bool eofSeen = false;

    auto readCallback = [&](int pfd, int cfd) -> bool {
      std::string lineBuf;

      if (cfd != 1) {
        EXPECT_EQ(2, cfd);
        EXPECT_TRUE(readToString(pfd, lineBuf, 1));
        EXPECT_EQ(0, lineBuf.size());
        return true;
      }

      EXPECT_FALSE(eofSeen);

      std::string expected;

      if (rcount < lineCount) {
        expected = folly::to<std::string>("a successful test ", rcount++, "\n");
      }

      EXPECT_EQ(wcount, rcount);

      // Not entirely kosher, we should handle partial reads, but this is
      // fine for reads <= PIPE_BUF
      bool atEof = readToString(pfd, lineBuf, expected.size() + 1);
      if (atEof) {
        // EOF only expected after we finished reading
        EXPECT_EQ(lineCount, rcount);
        eofSeen = true;
      }

      EXPECT_EQ(expected, lineBuf);

      if (wcount != lineCount) { // still more to write...
        proc.enableNotifications(0, true);
      }

      return eofSeen;
    };

    proc.communicate(readCallback, writeCallback);

    EXPECT_EQ(lineCount, wcount);
    EXPECT_EQ(lineCount, rcount);
    EXPECT_TRUE(eofSeen);

    EXPECT_EQ(0, proc.wait().exitStatus());
  });
}

TEST(CommunicateSubprocessTest, TakeOwnershipOfPipes) {
  std::vector<Subprocess::ChildPipe> pipes;
  {
    Subprocess proc(
        "echo $'oh\\nmy\\ncat' | wc -l &", Subprocess::Options().pipeStdout());
    pipes = proc.takeOwnershipOfPipes();
    proc.waitChecked();
  }
  EXPECT_EQ(1, pipes.size());
  EXPECT_EQ(1, pipes[0].childFd);

  char buf[10];
  EXPECT_EQ(2, readFull(pipes[0].pipe.fd(), buf, 10));
  buf[2] = 0;
  EXPECT_EQ("3\n", std::string(buf));
}

TEST(CommunicateSubprocessTest, RedirectStdioToDevNull) {
  std::vector<std::string> cmd({
      "stat",
      "-Lc",
      "%t:%T",
      "/dev/null",
      "/dev/stdin",
      "/dev/stderr",
  });
  auto options =
      Subprocess::Options()
          .pipeStdout()
          .stdinFd(folly::Subprocess::DEV_NULL)
          .stderrFd(folly::Subprocess::DEV_NULL)
          .usePath();
  Subprocess proc(cmd, options);
  auto out = proc.communicateIOBuf();

  fbstring stdoutStr;
  if (out.first.front()) {
    stdoutStr = out.first.move()->moveToFbString();
  }
  LOG(ERROR) << stdoutStr;
  std::vector<StringPiece> stdoutLines;
  split('\n', stdoutStr, stdoutLines);

  // 3 lines + empty string due to trailing newline
  EXPECT_EQ(stdoutLines.size(), 4);
  EXPECT_EQ(stdoutLines[0], stdoutLines[1]);
  EXPECT_EQ(stdoutLines[0], stdoutLines[2]);

  EXPECT_EQ(0, proc.wait().exitStatus());
}

TEST(CloseOtherDescriptorsSubprocessTest, ClosesFileDescriptors) {
  // Open another filedescriptor and check to make sure that it is not opened in
  // child process
  int fd = fileops::open("/", O_RDONLY);
  auto guard = makeGuard([fd] { fileops::close(fd); });
  auto options = Subprocess::Options().closeOtherFds().pipeStdout();
  Subprocess proc(
      std::vector<std::string>{"/bin/ls", "/proc/self/fd"}, options);
  auto p = proc.communicate();
  // stdin, stdout, stderr, and /proc/self/fd should be fds [0,3] in the child
  EXPECT_EQ("0\n1\n2\n3\n", p.first);
  proc.wait();
}

TEST(KeepFileOpenSubprocessTest, KeepsFileOpen) {
  auto f0 = folly::File{"/dev/null"};
  auto f1 = f0.dup();
  auto f2 = f0.dup();
  auto f3 = f0.dup();

  f0.close(); // make space for fd 3, for ls to open /proc/self/fd

  auto options =
      Subprocess::Options()
          .closeOtherFds()
          .pipeStdout()
          .fd(f1.fd(), Subprocess::NO_CLOEXEC)
          .fd(f2.fd(), f2.fd());
  Subprocess proc(
      std::vector<std::string>{"/bin/ls", "/proc/self/fd"}, options);
  auto p = proc.communicate();
  proc.wait();
  int fds[] = {0, 1, 2, 3, f1.fd(), f2.fd()};
  std::sort(std::begin(fds), std::end(fds));
  EXPECT_EQ(fmt::format("{}\n", fmt::join(fds, "\n")), p.first);
}

static_assert(
    Subprocess::Options::kPidBufferMinSize ==
    std::numeric_limits<pid_t>::digits10 + 2);

TEST(WritePidIntoBufTest, WritesPidIntoBufTooSmall) {
  constexpr size_t size = Subprocess::Options::kPidBufferMinSize;
  char buf[size - 1] = {};
  auto options = Subprocess::Options();
  EXPECT_THROW(options.addPrintPidToBuffer(buf), std::invalid_argument);
}

TEST(WritePidIntoBufTest, WritesPidIntoBuf) {
  constexpr size_t size = Subprocess::Options::kPidBufferMinSize;
  char buf[size] = {};
  std::memset(buf, 0xA5, size);
  auto options = Subprocess::Options().addPrintPidToBuffer(buf);
  Subprocess proc(std::vector<std::string>{"/bin/true"}, options);
  EXPECT_EQ(fmt::format("{}", proc.pid()), buf);
  proc.wait();
}

TEST(WritePidIntoBufTest, WritesPidIntoBufExampleEnvVar) {
  // this test effectively duplicates WritesPidIntoBuf but may serve as a
  // reference for how to use this feature with environment-variable storage
  //
  // systemd does something like this:
  // https://www.freedesktop.org/software/systemd/man/latest/systemd.exec.html#%24SYSTEMD_EXEC_PID
  constexpr size_t size = Subprocess::Options::kPidBufferMinSize;
  constexpr auto prefix = "FOLLY_TEST_SUBPROCESS_PID="sv;
  std::vector<std::string> env;
  env.emplace_back("FOLLY_TEST_GREETING=hello world");
  auto& var = env.emplace_back(prefix);
  var.resize(prefix.size() + size); // must be stable! no more changes to env!
  auto buf = folly::span{var}.subspan(prefix.size());
  auto options = Subprocess::Options().pipeStdout().addPrintPidToBuffer(buf);
  Subprocess proc(std::vector<std::string>{"/bin/env"}, options, nullptr, &env);
  auto pid = proc.pid();
  auto p = proc.communicate();
  proc.wait();
  std::vector<std::string_view> lines;
  folly::split('\n', p.first, lines);
  EXPECT_THAT(lines, testing::Contains(fmt::format("{}{}", prefix, pid)));
}

#if defined(__linux__)

TEST(SetSignalMask, KeepsExistingMask) {
  // the /proc filesystem, including /proc/self/status, is linux-specific
  ASSERT_EQ(0, readSignalMask(getCurrentSignalMask()));
  ScopedSignalMaskOverride guard{std::array{SIGURG, SIGCHLD}};
  auto options = Subprocess::Options().pipeStdout();
  Subprocess proc(
      std::vector<std::string>{"/bin/cat", "/proc/self/status"}, options);
  auto p = proc.communicate();
  proc.wait();
  auto line = readOneLineOfProcSelfStatus(p.first, "SigBlk");
  auto expected = (1 << (SIGURG - 1)) | (1 << (SIGCHLD - 1));
  EXPECT_EQ(fmt::format("{:016x}", expected), line);
}

TEST(SetSignalMask, CanOverrideExistingMask) {
  // the /proc filesystem, including /proc/self/status, is linux-specific
  ASSERT_EQ(0, readSignalMask(getCurrentSignalMask()));
  ScopedSignalMaskOverride guard{std::array{SIGURG, SIGCHLD}};
  auto sigmask = makeSignalMask(std::array{SIGUSR1, SIGUSR2});
  auto options = Subprocess::Options().pipeStdout().setSignalMask(sigmask);
  Subprocess proc(
      std::vector<std::string>{"/bin/cat", "/proc/self/status"}, options);
  auto p = proc.communicate();
  proc.wait();
  auto line = readOneLineOfProcSelfStatus(p.first, "SigBlk");
  auto expected = (1 << (SIGUSR1 - 1)) | (1 << (SIGUSR2 - 1));
  EXPECT_EQ(fmt::format("{:016x}", expected), line);
}

TEST(SetUserGroupId, KeepsExisting) {
  auto options = Subprocess::Options().pipeStdout();
  Subprocess proc(
      std::vector<std::string>{"/bin/cat", "/proc/self/status"}, options);
  auto p = proc.communicate();
  proc.wait();
  auto uidline = readOneLineOfProcSelfStatus(p.first, "Uid");
  auto gidline = readOneLineOfProcSelfStatus(p.first, "Gid");
  auto [uid, euid, gid, egid] =
      std::tuple{getuid(), geteuid(), getgid(), getegid()};
  EXPECT_EQ(euid, uid);
  EXPECT_EQ(egid, gid);
  EXPECT_EQ(fmt::format("{}\t{}\t{}\t{}", uid, euid, uid, uid), uidline);
  EXPECT_EQ(fmt::format("{}\t{}\t{}\t{}", gid, egid, gid, gid), gidline);
}

TEST(SetUserGroupId, CanOverrideAndReportFailure) {
  // without elevated capabilities, the process cannot switch user/group
  // which makes writing the unit-test for that impossible; here we just
  // check the errors
  auto options = Subprocess::Options().pipeStdout();
  int errnum[4] = {};
  options.setUid(0, to_shared_ptr_non_owning(errnum + 0));
  options.setGid(0, to_shared_ptr_non_owning(errnum + 1));
  options.setEUid(0, to_shared_ptr_non_owning(errnum + 2));
  options.setEGid(0, to_shared_ptr_non_owning(errnum + 3));
  Subprocess proc(
      std::vector<std::string>{"/bin/cat", "/proc/self/status"}, options);
  auto p = proc.communicate();
  proc.wait();
  auto uidline = readOneLineOfProcSelfStatus(p.first, "Uid");
  auto gidline = readOneLineOfProcSelfStatus(p.first, "Gid");
  auto [uid, euid, gid, egid] = std::tuple{
      errnum[0] ? getuid() : 0,
      errnum[2] ? geteuid() : 0,
      errnum[1] ? getgid() : 0,
      errnum[3] ? getegid() : 0};
  EXPECT_EQ(euid, uid);
  EXPECT_EQ(egid, gid);
  EXPECT_EQ(fmt::format("{}\t{}\t{}\t{}", uid, euid, uid, uid), uidline);
  EXPECT_EQ(fmt::format("{}\t{}\t{}\t{}", gid, egid, gid, gid), gidline);
}

TEST(SetLinuxCGroup, CanSetCGroupFdAbsent) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgdirfd = ::open(cgdir.path().native().c_str(), O_DIRECTORY | O_CLOEXEC);
  auto cgdirfdGuard = folly::makeGuard([&] { ::close(cgdirfd); });
  auto options = Subprocess::Options();
  options.setLinuxCGroupFd(cgdirfd);
  EXPECT_THROW(
      Subprocess(std::vector{"/bin/true"s}, options), SubprocessSpawnError);
}

TEST(SetLinuxCGroup, CanSetCGroupFdAbsentIntoErrnum) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgdirfd = ::open(cgdir.path().native().c_str(), O_DIRECTORY | O_CLOEXEC);
  auto cgdirfdGuard = folly::makeGuard([&] { ::close(cgdirfd); });
  auto options = Subprocess::Options();
  int errnum = 0;
  options.setLinuxCGroupFd(cgdirfd, to_shared_ptr_non_owning(&errnum));
  Subprocess proc(std::vector{"/bin/true"s}, options);
  EXPECT_EQ(ENOENT, errnum) << ::strerror(errnum);
  proc.wait();
  EXPECT_EQ(0, proc.returnCode().exitStatus());
}

TEST(SetLinuxCGroup, CanSetCGroupFdPresent) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgdirfd = ::open(cgdir.path().native().c_str(), O_DIRECTORY | O_CLOEXEC);
  auto cgdirfdGuard = folly::makeGuard([&] { ::close(cgdirfd); });
  auto cgprocs = cgdir.path() / "cgroup.procs";
  ::creat(cgprocs.native().c_str(), 0755); // rm'd with cgdir
  auto options = Subprocess::Options();
  options.setLinuxCGroupFd(cgdirfd);
  Subprocess proc(std::vector{"/bin/true"s}, options);
  std::string s;
  EXPECT_TRUE(readFile(cgprocs.native().c_str(), s));
  EXPECT_EQ("0", s);
  proc.wait();
}

TEST(SetLinuxCGroup, CanSetCGroupFdPresentIntoErrnum) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgdirfd = ::open(cgdir.path().native().c_str(), O_DIRECTORY | O_CLOEXEC);
  auto cgdirfdGuard = folly::makeGuard([&] { ::close(cgdirfd); });
  auto cgprocs = cgdir.path() / "cgroup.procs";
  ::creat(cgprocs.native().c_str(), 0755); // rm'd with cgdir
  auto options = Subprocess::Options();
  int errnum = 0;
  options.setLinuxCGroupFd(cgdirfd, to_shared_ptr_non_owning(&errnum));
  Subprocess proc(std::vector{"/bin/true"s}, options);
  EXPECT_EQ(0, errnum) << ::strerror(errnum);
  std::string s;
  EXPECT_TRUE(readFile(cgprocs.native().c_str(), s));
  EXPECT_EQ("0", s);
  proc.wait();
}

TEST(SetLinuxCGroup, CanSetCGroupFdPresentProcsNoOpen) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgprocs = cgdir.path() / "cgroup.procs";
  ::creat(cgprocs.native().c_str(), 0); // rm'd with cgdir
  auto cgdirfd = ::open(cgdir.path().native().c_str(), O_DIRECTORY | O_CLOEXEC);
  auto cgdirfdGuard = folly::makeGuard([&] { ::close(cgdirfd); });
  auto options = Subprocess::Options();
  options.setLinuxCGroupFd(cgdirfd);
  EXPECT_THROW(
      Subprocess(std::vector{"/bin/true"s}, options), SubprocessSpawnError);
}

TEST(SetLinuxCGroup, CanSetCGroupFdPresentProcsNoOpenIntoErrnum) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgprocs = cgdir.path() / "cgroup.procs";
  ::creat(cgprocs.native().c_str(), 0); // rm'd with cgdir
  auto cgdirfd = ::open(cgdir.path().native().c_str(), O_DIRECTORY | O_CLOEXEC);
  auto cgdirfdGuard = folly::makeGuard([&] { ::close(cgdirfd); });
  auto options = Subprocess::Options();
  int errnum = 0;
  options.setLinuxCGroupFd(cgdirfd, to_shared_ptr_non_owning(&errnum));
  Subprocess proc(std::vector{"/bin/true"s}, options);
  EXPECT_EQ(EACCES, errnum) << ::strerror(errnum);
  proc.wait();
  EXPECT_EQ(0, proc.returnCode().exitStatus());
}

TEST(SetLinuxCGroup, CanSetCGroupPathAbsent) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto options = Subprocess::Options();
  options.setLinuxCGroupPath(cgdir.path().string());
  EXPECT_THROW(
      Subprocess(std::vector{"/bin/true"s}, options), SubprocessSpawnError);
}

TEST(SetLinuxCGroup, CanSetCGroupPathAbsentIntoErrnum) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto options = Subprocess::Options();
  int errnum = 0;
  options.setLinuxCGroupPath(
      cgdir.path().string(), to_shared_ptr_non_owning(&errnum));
  Subprocess proc(std::vector{"/bin/true"s}, options);
  EXPECT_EQ(ENOENT, errnum) << ::strerror(errnum);
  proc.wait();
  EXPECT_EQ(0, proc.returnCode().exitStatus());
}

TEST(SetLinuxCGroup, CanSetCGroupPathPresent) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgprocs = cgdir.path() / "cgroup.procs";
  ::creat(cgprocs.native().c_str(), 0755); // rm'd with cgdir
  auto options = Subprocess::Options();
  options.setLinuxCGroupPath(cgdir.path().string());
  Subprocess proc(std::vector{"/bin/true"s}, options);
  std::string s;
  EXPECT_TRUE(readFile(cgprocs.native().c_str(), s));
  EXPECT_EQ("0", s);
  proc.wait();
}

TEST(SetLinuxCGroup, CanSetCGroupPathPresentIntoErrnum) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgprocs = cgdir.path() / "cgroup.procs";
  ::creat(cgprocs.native().c_str(), 0755); // rm'd with cgdir
  auto options = Subprocess::Options();
  int errnum = 0;
  options.setLinuxCGroupPath(
      cgdir.path().string(), to_shared_ptr_non_owning(&errnum));
  Subprocess proc(std::vector{"/bin/true"s}, options);
  EXPECT_EQ(0, errnum) << ::strerror(errnum);
  std::string s;
  EXPECT_TRUE(readFile(cgprocs.native().c_str(), s));
  EXPECT_EQ("0", s);
  proc.wait();
}

TEST(SetLinuxCGroup, CanSetCGroupPathPresentProcsNoOpen) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgprocs = cgdir.path() / "cgroup.procs";
  ::creat(cgprocs.native().c_str(), 0); // rm'd with cgdir
  auto options = Subprocess::Options();
  options.setLinuxCGroupPath(cgdir.path().string());
  EXPECT_THROW(
      Subprocess(std::vector{"/bin/true"s}, options), SubprocessSpawnError);
}

TEST(SetLinuxCGroup, CanSetCGroupPathPresentProcsNoOpenIntoErrnum) {
  folly::test::TemporaryDirectory cgdir; // not a real cgroup dir
  auto cgprocs = cgdir.path() / "cgroup.procs";
  ::creat(cgprocs.native().c_str(), 0); // rm'd with cgdir
  auto options = Subprocess::Options();
  int errnum = 0;
  options.setLinuxCGroupPath(
      cgdir.path().string(), to_shared_ptr_non_owning(&errnum));
  Subprocess proc(std::vector{"/bin/true"s}, options);
  EXPECT_EQ(EACCES, errnum) << ::strerror(errnum);
  proc.wait();
  EXPECT_EQ(0, proc.returnCode().exitStatus());
}

#endif

TEST(SetRLimit, SetRLimitSuccess) {
  rlimit limit;
  ::getrlimit(RLIMIT_MEMLOCK, &limit);
  auto limit2 = limit;
  limit2.rlim_cur -= ::sysconf(_SC_PAGESIZE);
  auto options = Subprocess::Options().pipeStdout();
  options.addRLimit(RLIMIT_MEMLOCK, limit2);
  Subprocess proc(std::vector{"/bin/ulimit"s, "-l"s}, options);
  auto p = proc.communicate();
  proc.wait();
  EXPECT_EQ(fmt::format("{}\n", limit2.rlim_cur / 1024), p.first);
}

TEST(SetRLimit, SetRLimitFailure) {
  rlimit limit;
  ::getrlimit(RLIMIT_MEMLOCK, &limit);
  auto limit2 = limit;
  limit2.rlim_cur = limit2.rlim_max * 2;
  auto options = Subprocess::Options().pipeStdout();
  options.addRLimit(RLIMIT_MEMLOCK, limit2);
  EXPECT_THROW(
      Subprocess(std::vector{"/bin/ulimit"s, "-l"s}, options),
      SubprocessSpawnError);
}

TEST(SetRLimit, SetRLimitFailureIntoErrnum) {
  rlimit limit;
  ::getrlimit(RLIMIT_MEMLOCK, &limit);
  auto limit2 = limit;
  limit2.rlim_cur = limit2.rlim_max * 2;
  auto options = Subprocess::Options().pipeStdout();
  int errnum = 0;
  options.addRLimit(RLIMIT_MEMLOCK, limit2, to_shared_ptr_non_owning(&errnum));
  Subprocess proc(std::vector{"/bin/ulimit"s, "-l"s}, options);
  EXPECT_EQ(EINVAL, errnum);
  auto p = proc.communicate();
  proc.wait();
  EXPECT_EQ(fmt::format("{}\n", limit.rlim_cur / 1024), p.first);
}
