/*
 * Copyright 2014 Facebook, Inc.
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

#include "folly/experimental/TestUtil.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include <system_error>

#include <boost/algorithm/string.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::test;

TEST(TemporaryFile, Simple) {
  int fd = -1;
  char c = 'x';
  {
    TemporaryFile f;
    EXPECT_FALSE(f.path().empty());
    EXPECT_TRUE(f.path().is_absolute());
    fd = f.fd();
    EXPECT_LE(0, fd);
    ssize_t r = write(fd, &c, 1);
    EXPECT_EQ(1, r);
  }

  // The file must have been closed.  This assumes that no other thread
  // has opened another file in the meanwhile, which is a sane assumption
  // to make in this test.
  ssize_t r = write(fd, &c, 1);
  int savedErrno = errno;
  EXPECT_EQ(-1, r);
  EXPECT_EQ(EBADF, savedErrno);
}

TEST(TemporaryFile, Prefix) {
  TemporaryFile f("Foo");
  EXPECT_TRUE(f.path().is_absolute());
  EXPECT_TRUE(boost::algorithm::starts_with(f.path().filename().native(),
                                            "Foo"));
}

TEST(TemporaryFile, PathPrefix) {
  TemporaryFile f("Foo", ".");
  EXPECT_EQ(fs::path("."), f.path().parent_path());
  EXPECT_TRUE(boost::algorithm::starts_with(f.path().filename().native(),
                                            "Foo"));
}

TEST(TemporaryFile, NoSuchPath) {
  EXPECT_THROW({TemporaryFile f("", "/no/such/path");},
               std::system_error);
}

void testTemporaryDirectory(TemporaryDirectory::Scope scope) {
  fs::path path;
  {
    TemporaryDirectory d("", "", scope);
    path = d.path();
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(path.is_absolute());
    EXPECT_TRUE(fs::exists(path));
    EXPECT_TRUE(fs::is_directory(path));

    fs::path fp = path / "bar";
    int fd = open(fp.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    EXPECT_NE(fd, -1);
    close(fd);

    TemporaryFile f("Foo", d.path());
    EXPECT_EQ(d.path(), f.path().parent_path());
  }
  bool exists = (scope == TemporaryDirectory::Scope::PERMANENT);
  EXPECT_EQ(exists, fs::exists(path));
}

TEST(TemporaryDirectory, Permanent) {
  testTemporaryDirectory(TemporaryDirectory::Scope::PERMANENT);
}

TEST(TemporaryDirectory, DeleteOnDestruction) {
  testTemporaryDirectory(TemporaryDirectory::Scope::DELETE_ON_DESTRUCTION);
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}

