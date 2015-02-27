/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/File.h>

#include <mutex>

#include <boost/thread/locks.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include <folly/Benchmark.h>
#include <folly/String.h>
#include <folly/Subprocess.h>
#include <folly/experimental/io/FsUtil.h>
#include <folly/experimental/TestUtil.h>

using namespace folly;
using namespace folly::test;

namespace {
void expectWouldBlock(ssize_t r) {
  int savedErrno = errno;
  EXPECT_EQ(-1, r);
  EXPECT_EQ(EAGAIN, savedErrno) << errnoStr(errno);
}
void expectOK(ssize_t r) {
  int savedErrno = errno;
  EXPECT_LE(0, r) << ": errno=" << errnoStr(errno);
}
}  // namespace

TEST(File, Simple) {
  // Open a file, ensure it's indeed open for reading
  char buf = 'x';
  {
    File f("/etc/hosts");
    EXPECT_NE(-1, f.fd());
    EXPECT_EQ(1, ::read(f.fd(), &buf, 1));
    f.close();
    EXPECT_EQ(-1, f.fd());
  }
}

TEST(File, OwnsFd) {
  // Wrap a file descriptor, make sure that ownsFd works
  // We'll test that the file descriptor is closed by closing the writing
  // end of a pipe and making sure that a non-blocking read from the reading
  // end returns 0.

  char buf = 'x';
  int p[2];
  expectOK(::pipe(p));
  int flags = ::fcntl(p[0], F_GETFL);
  expectOK(flags);
  expectOK(::fcntl(p[0], F_SETFL, flags | O_NONBLOCK));
  expectWouldBlock(::read(p[0], &buf, 1));
  {
    File f(p[1]);
    EXPECT_EQ(p[1], f.fd());
  }
  // Ensure that moving the file doesn't close it
  {
    File f(p[1]);
    EXPECT_EQ(p[1], f.fd());
    File f1(std::move(f));
    EXPECT_EQ(-1, f.fd());
    EXPECT_EQ(p[1], f1.fd());
  }
  expectWouldBlock(::read(p[0], &buf, 1));  // not closed
  {
    File f(p[1], true);
    EXPECT_EQ(p[1], f.fd());
  }
  ssize_t r = ::read(p[0], &buf, 1);  // eof
  expectOK(r);
  EXPECT_EQ(0, r);
  ::close(p[0]);
}

TEST(File, Release) {
  File in(STDOUT_FILENO, false);
  CHECK_EQ(STDOUT_FILENO, in.release());
  CHECK_EQ(-1, in.release());
}

#define EXPECT_CONTAINS(haystack, needle) \
  EXPECT_NE(::std::string::npos, ::folly::StringPiece(haystack).find(needle)) \
    << "Haystack: '" << haystack << "'\nNeedle: '" << needle << "'";

TEST(File, UsefulError) {
  try {
    File("does_not_exist.txt", 0, 0666);
  } catch (const std::runtime_error& e) {
    EXPECT_CONTAINS(e.what(), "does_not_exist.txt");
    EXPECT_CONTAINS(e.what(), "0666");
  }
}

TEST(File, Truthy) {
  File temp = File::temporary();

  EXPECT_TRUE(bool(temp));

  if (temp) {
    ;
  } else {
    EXPECT_FALSE(true);
  }

  if (File file = File::temporary()) {
    ;
  } else {
    EXPECT_FALSE(true);
  }

  EXPECT_FALSE(bool(File()));
  if (File()) {
    EXPECT_TRUE(false);
  }
  if (File notOpened = File()) {
    EXPECT_TRUE(false);
  }
}

TEST(File, Locks) {
  typedef std::unique_lock<File> Lock;
  typedef boost::shared_lock<File> SharedLock;

  // Find out where we are.
  static constexpr size_t pathLength = 2048;
  char buf[pathLength + 1];
  int r = readlink("/proc/self/exe", buf, pathLength);
  CHECK_ERR(r);
  buf[r] = '\0';

  // NOTE(agallagher): Our various internal build systems layout built
  // binaries differently, so the two layouts below.
  fs::path me(buf);
  auto helper_basename = "file_test_lock_helper";
  fs::path helper;
  if (fs::exists(me.parent_path() / helper_basename)) {
    helper = me.parent_path() / helper_basename;
  } else if (fs::exists(
      me.parent_path().parent_path() / helper_basename / helper_basename)) {
    helper = me.parent_path().parent_path()
      / helper_basename / helper_basename;
  } else {
    throw std::runtime_error(
      folly::to<std::string>("cannot find helper ", helper_basename));
  }

  TemporaryFile tempFile;
  File f(tempFile.fd());

  enum LockMode { EXCLUSIVE, SHARED };
  auto testLock = [&] (LockMode mode, bool expectedSuccess) {
    auto ret =
      Subprocess({helper.native(),
                  mode == SHARED ? "-s" : "-x",
                  tempFile.path().native()}).wait();
    EXPECT_TRUE(ret.exited());
    if (ret.exited()) {
      EXPECT_EQ(expectedSuccess ? 0 : 42, ret.exitStatus());
    }
  };

  // Make sure nothing breaks and things compile.
  {
    Lock lock(f);
  }

  {
    SharedLock lock(f);
  }

  {
    Lock lock(f, std::defer_lock);
    EXPECT_TRUE(lock.try_lock());
  }

  {
    SharedLock lock(f, boost::defer_lock);
    EXPECT_TRUE(lock.try_lock());
  }

  // X blocks X
  {
    Lock lock(f);
    testLock(EXCLUSIVE, false);
  }

  // X blocks S
  {
    Lock lock(f);
    testLock(SHARED, false);
  }

  // S blocks X
  {
    SharedLock lock(f);
    testLock(EXCLUSIVE, false);
  }

  // S does not block S
  {
    SharedLock lock(f);
    testLock(SHARED, true);
  }
}
