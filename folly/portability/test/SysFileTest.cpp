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

#include <folly/portability/SysFile.h>

#include <cerrno>

#ifdef _WIN32
#include <future>
#include <thread>

#include <folly/ScopeGuard.h>
#include <folly/portability/Windows.h>
#endif

#include <folly/File.h>
#include <folly/Portability.h>
#include <folly/portability/Fcntl.h>
#include <folly/portability/Unistd.h>
#include <folly/testing/TestUtil.h>

#include <gtest/gtest.h>

using folly::File;
using folly::test::TemporaryFile;

// flock() locks are per-open-file-description on POSIX and per-HANDLE on
// Windows, so two independent opens of the same file contend even within a
// single process. TemporaryFile keeps the file on disk until destruction, so a
// second open of its path is safe on both platforms.
// A non-blocking lock request that loses to an already-held lock must report
// EWOULDBLOCK. The Windows flock() shim previously returned -1 without setting
// errno, which made folly::File::try_lock() throw instead of returning false.
TEST(SysFileTest, FlockNonBlockingContentionSetsEwouldblock) {
  TemporaryFile tempFile;
  File second(tempFile.path().string().c_str(), O_RDWR);

  ASSERT_EQ(0, flock(tempFile.fd(), LOCK_EX));

  errno = 0;
  const int result = flock(second.fd(), LOCK_EX | LOCK_NB);
  const int savedErrno = errno;
  EXPECT_EQ(-1, result);
  EXPECT_EQ(EWOULDBLOCK, savedErrno);

  ASSERT_EQ(0, flock(tempFile.fd(), LOCK_UN));

  // Once the first lock is released, the second descriptor can acquire it.
  EXPECT_EQ(0, flock(second.fd(), LOCK_EX | LOCK_NB));
  EXPECT_EQ(0, flock(second.fd(), LOCK_UN));
}

// Regression test for the reported symptom: try_lock() must return false on
// contention rather than throwing std::system_error.
TEST(SysFileTest, FileTryLockReturnsFalseOnContention) {
  TemporaryFile tempFile;
  File f1(tempFile.path().string().c_str(), O_RDWR);
  File f2(tempFile.path().string().c_str(), O_RDWR);

  ASSERT_TRUE(f1.try_lock());
  EXPECT_FALSE(f2.try_lock());
  f1.unlock();

  EXPECT_TRUE(f2.try_lock());
  f2.unlock();
}

TEST(SysFileTest, SharedLocksCoexistAndBlockExclusiveLock) {
  TemporaryFile tempFile;
  File second(tempFile.path().string().c_str(), O_RDWR);
  File third(tempFile.path().string().c_str(), O_RDWR);

  ASSERT_EQ(0, flock(tempFile.fd(), LOCK_SH));
  ASSERT_EQ(0, flock(second.fd(), LOCK_SH | LOCK_NB));

  errno = 0;
  const int result = flock(third.fd(), LOCK_EX | LOCK_NB);
  const int savedErrno = errno;
  EXPECT_EQ(-1, result);
  EXPECT_EQ(EWOULDBLOCK, savedErrno);

  ASSERT_EQ(0, flock(second.fd(), LOCK_UN));
  ASSERT_EQ(0, flock(tempFile.fd(), LOCK_UN));
  EXPECT_EQ(0, flock(third.fd(), LOCK_EX | LOCK_NB));
  EXPECT_EQ(0, flock(third.fd(), LOCK_UN));
}

TEST(SysFileTest, UnlockingUnlockedFileSucceeds) {
  TemporaryFile tempFile;
  EXPECT_EQ(0, flock(tempFile.fd(), LOCK_UN));
}

// POSIX flock(2) requires exactly one of LOCK_SH, LOCK_EX, LOCK_UN, with no
// other bit set besides LOCK_NB; naming none, combining them, or setting an
// unrecognized bit is invalid everywhere, but the exact outcome is
// platform-specific. Linux (glibc) and this shim's Windows implementation
// switch on the operation with only LOCK_NB masked off, so anything but a
// single exact match is EINVAL. BSD/XNU (macOS, FreeBSD) checks LOCK_UN
// unconditionally first -- so LOCK_EX | LOCK_UN silently unlocks -- then
// tests LOCK_SH/LOCK_EX independently rather than for an exact match, so it
// silently accepts LOCK_SH | LOCK_EX and an unrecognized extra bit as
// LOCK_EX, and reports EBADF only when neither LOCK_SH nor LOCK_EX is named.
TEST(SysFileTest, InvalidOperationSetsEinval) {
  const bool isBsdFlock = folly::kIsApple || folly::kIsFreeBSD;
  TemporaryFile tempFile;

  errno = 0;
  const int neitherResult = flock(tempFile.fd(), LOCK_NB);
  const int neitherErrno = errno;
  EXPECT_EQ(-1, neitherResult);
  EXPECT_EQ(isBsdFlock ? EBADF : EINVAL, neitherErrno);

  errno = 0;
  const int bothLockResult = flock(tempFile.fd(), LOCK_SH | LOCK_EX);
  const int bothLockErrno = errno;
  if (isBsdFlock) {
    EXPECT_EQ(0, bothLockResult);
    EXPECT_EQ(0, flock(tempFile.fd(), LOCK_UN));
  } else {
    EXPECT_EQ(-1, bothLockResult);
    EXPECT_EQ(EINVAL, bothLockErrno);
  }

  errno = 0;
  const int lockAndUnlockResult = flock(tempFile.fd(), LOCK_EX | LOCK_UN);
  const int lockAndUnlockErrno = errno;
  if (isBsdFlock) {
    EXPECT_EQ(0, lockAndUnlockResult);
  } else {
    EXPECT_EQ(-1, lockAndUnlockResult);
    EXPECT_EQ(EINVAL, lockAndUnlockErrno);
  }

  constexpr int kUnrecognizedBit = 0x100;
  errno = 0;
  const int extraBitResult = flock(tempFile.fd(), LOCK_EX | kUnrecognizedBit);
  const int extraBitErrno = errno;
  if (isBsdFlock) {
    EXPECT_EQ(0, extraBitResult);
    EXPECT_EQ(0, flock(tempFile.fd(), LOCK_UN));
  } else {
    EXPECT_EQ(-1, extraBitResult);
    EXPECT_EQ(EINVAL, extraBitErrno);
  }
}

// A file descriptor with no valid underlying handle must fail with EBADF,
// matching POSIX flock(2) on a bad descriptor. On Windows the shim maps the
// missing OS handle to EBADF; _get_osfhandle() can trip the CRT
// invalid-parameter handler for a bad fd, so the call is wrapped to keep debug
// builds from aborting (the wrapper is a no-op elsewhere).
TEST(SysFileTest, InvalidFdSetsEbadf) {
  int result = 0;
  int savedErrno = 0;
  folly::test::msvcSuppressAbortOnInvalidParams([&] {
    errno = 0;
    result = flock(-1, LOCK_EX | LOCK_NB);
    savedErrno = errno;
  });
  EXPECT_EQ(-1, result);
  EXPECT_EQ(EBADF, savedErrno);
}

#ifdef _WIN32
// ERROR_IO_PENDING is only reachable on a FILE_FLAG_OVERLAPPED handle, which
// folly::File never creates (see folly/portability/Fcntl.cpp), so build one
// directly: the blocking lock below must wait for the holder instead of
// failing with EIO. The non-blocking check resolves synchronously here
// (ERROR_LOCK_VIOLATION); LOCK_NB can still queue on some filesystems (see
// flockDoLock()), just not this local one.
TEST(SysFileTest, OverlappedHandleLockWaitsForQueuedRequest) {
  TemporaryFile tempFile;
  File holder(tempFile.path().string().c_str(), O_RDWR);
  ASSERT_TRUE(holder.try_lock());

  const HANDLE rawHandle = CreateFileA(
      tempFile.path().string().c_str(),
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED,
      nullptr);
  ASSERT_NE(INVALID_HANDLE_VALUE, rawHandle);
  // Owns rawHandle until _open_osfhandle() adopts it below, so a failed
  // adoption (or an ASSERT_* in between) cannot leak the handle.
  auto closeRawHandle = folly::makeGuard([&] { CloseHandle(rawHandle); });

  const int overlappedFd =
      _open_osfhandle(reinterpret_cast<intptr_t>(rawHandle), O_RDWR);
  ASSERT_NE(-1, overlappedFd);
  closeRawHandle.dismiss();
  // overlappedFd now owns rawHandle; closing the fd closes the handle too.
  auto closeFd = folly::makeGuard([&] { _close(overlappedFd); });

  errno = 0;
  const int result = flock(overlappedFd, LOCK_EX | LOCK_NB);
  const int savedErrno = errno;
  EXPECT_EQ(-1, result);
  EXPECT_EQ(EWOULDBLOCK, savedErrno);

  // blockingResult is only read after join(), since gtest assertions on a
  // non-main thread aren't reliably reported here. blockerReady proves only
  // that the blocker thread has started, not that its flock() call has
  // reached LockFileEx() yet, so a slow scheduler could let holder.unlock()
  // run first and grant the lock synchronously, skipping the queued-wait
  // path this test targets; flock() must return 0 either way, so this
  // cannot make the test flake, only skip that path.
  std::promise<void> blockerReady;
  int blockingResult = -1;
  std::thread blocker([&] {
    blockerReady.set_value();
    blockingResult = flock(overlappedFd, LOCK_EX);
  });
  blockerReady.get_future().wait();
  holder.unlock();
  blocker.join();
  EXPECT_EQ(0, blockingResult);

  EXPECT_EQ(0, flock(overlappedFd, LOCK_UN));
}
#endif
