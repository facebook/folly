/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/FileUtil.h>
#include <folly/detail/FileUtilDetail.h>
#include <folly/experimental/TestUtil.h>

#include <deque>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <folly/Range.h>
#include <folly/String.h>

namespace folly { namespace test {

using namespace fileutil_detail;
using namespace std;

namespace {

class Reader {
 public:
  Reader(off_t offset, StringPiece data, std::deque<ssize_t> spec);

  // write-like
  ssize_t operator()(int fd, void* buf, size_t count);

  // pwrite-like
  ssize_t operator()(int fd, void* buf, size_t count, off_t offset);

  // writev-like
  ssize_t operator()(int fd, const iovec* iov, int count);

  // pwritev-like
  ssize_t operator()(int fd, const iovec* iov, int count, off_t offset);

  const std::deque<ssize_t> spec() const { return spec_; }

 private:
  ssize_t nextSize();

  off_t offset_;
  StringPiece data_;
  std::deque<ssize_t> spec_;
};

Reader::Reader(off_t offset, StringPiece data, std::deque<ssize_t> spec)
  : offset_(offset),
    data_(data),
    spec_(std::move(spec)) {
}

ssize_t Reader::nextSize() {
  if (spec_.empty()) {
    throw std::runtime_error("spec empty");
  }
  ssize_t n = spec_.front();
  spec_.pop_front();
  if (n <= 0) {
    if (n == -1) {
      errno = EIO;
    }
    spec_.clear();  // so we fail if called again
  } else {
    offset_ += n;
  }
  return n;
}

ssize_t Reader::operator()(int /* fd */, void* buf, size_t count) {
  ssize_t n = nextSize();
  if (n <= 0) {
    return n;
  }
  if (size_t(n) > count) {
    throw std::runtime_error("requested count too small");
  }
  memcpy(buf, data_.data(), n);
  data_.advance(n);
  return n;
}

ssize_t Reader::operator()(int fd, void* buf, size_t count, off_t offset) {
  EXPECT_EQ(offset_, offset);
  return operator()(fd, buf, count);
}

ssize_t Reader::operator()(int /* fd */, const iovec* iov, int count) {
  ssize_t n = nextSize();
  if (n <= 0) {
    return n;
  }
  ssize_t remaining = n;
  for (; count != 0 && remaining != 0; ++iov, --count) {
    ssize_t len = std::min(remaining, ssize_t(iov->iov_len));
    memcpy(iov->iov_base, data_.data(), len);
    data_.advance(len);
    remaining -= len;
  }
  if (remaining != 0) {
    throw std::runtime_error("requested total size too small");
  }
  return n;
}

ssize_t Reader::operator()(int fd, const iovec* iov, int count, off_t offset) {
  EXPECT_EQ(offset_, offset);
  return operator()(fd, iov, count);
}

}  // namespace

class FileUtilTest : public ::testing::Test {
 protected:
  FileUtilTest();

  Reader reader(std::deque<ssize_t> spec);

  std::string in_;
  std::vector<std::pair<size_t, Reader>> readers_;
};

FileUtilTest::FileUtilTest()
  : in_("1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") {
  CHECK_EQ(62, in_.size());

  readers_.emplace_back(0, reader({0}));
  readers_.emplace_back(62, reader({62}));
  readers_.emplace_back(62, reader({62, -1}));  // error after end (not called)
  readers_.emplace_back(61, reader({61, 0}));
  readers_.emplace_back(-1, reader({61, -1}));  // error before end
  readers_.emplace_back(62, reader({31, 31}));
  readers_.emplace_back(62, reader({1, 10, 20, 10, 1, 20}));
  readers_.emplace_back(61, reader({1, 10, 20, 10, 20, 0}));
  readers_.emplace_back(41, reader({1, 10, 20, 10, 0}));
  readers_.emplace_back(-1, reader({1, 10, 20, 10, 20, -1}));
}

Reader FileUtilTest::reader(std::deque<ssize_t> spec) {
  return Reader(42, in_, std::move(spec));
}

TEST_F(FileUtilTest, read) {
  for (auto& p : readers_) {
    std::string out(in_.size(), '\0');
    EXPECT_EQ(p.first, wrapFull(p.second, 0, &out[0], out.size()));
    if (p.first != (typeof(p.first))(-1)) {
      EXPECT_EQ(in_.substr(0, p.first), out.substr(0, p.first));
    }
  }
}

TEST_F(FileUtilTest, pread) {
  for (auto& p : readers_) {
    std::string out(in_.size(), '\0');
    EXPECT_EQ(p.first, wrapFull(p.second, 0, &out[0], out.size(), off_t(42)));
    if (p.first != (typeof(p.first))(-1)) {
      EXPECT_EQ(in_.substr(0, p.first), out.substr(0, p.first));
    }
  }
}

class IovecBuffers {
 public:
  explicit IovecBuffers(std::initializer_list<size_t> sizes);
  explicit IovecBuffers(std::vector<size_t> sizes);

  std::vector<iovec> iov() const { return iov_; }  // yes, make a copy
  std::string join() const { return folly::join("", buffers_); }
  size_t size() const;

 private:
  std::vector<std::string> buffers_;
  std::vector<iovec> iov_;
};

IovecBuffers::IovecBuffers(std::initializer_list<size_t> sizes) {
  iov_.reserve(sizes.size());
  for (auto& s : sizes) {
    buffers_.push_back(std::string(s, '\0'));
  }
  for (auto& b : buffers_) {
    iovec iov;
    iov.iov_base = &b[0];
    iov.iov_len = b.size();
    iov_.push_back(iov);
  }
}

IovecBuffers::IovecBuffers(std::vector<size_t> sizes) {
  iov_.reserve(sizes.size());
  for (auto s : sizes) {
    buffers_.push_back(std::string(s, '\0'));
  }
  for (auto& b : buffers_) {
    iovec iov;
    iov.iov_base = &b[0];
    iov.iov_len = b.size();
    iov_.push_back(iov);
  }
}

size_t IovecBuffers::size() const {
  size_t s = 0;
  for (auto& b : buffers_) {
    s += b.size();
  }
  return s;
}

TEST_F(FileUtilTest, readv) {
  for (auto& p : readers_) {
    IovecBuffers buf({12, 19, 31});
    ASSERT_EQ(62, buf.size());

    auto iov = buf.iov();
    EXPECT_EQ(p.first, wrapvFull(p.second, 0, iov.data(), iov.size()));
    if (p.first != (typeof(p.first))(-1)) {
      EXPECT_EQ(in_.substr(0, p.first), buf.join().substr(0, p.first));
    }
  }
}

TEST(FileUtilTest2, wrapv) {
  TemporaryFile tempFile("file-util-test");
  std::vector<size_t> sizes;
  size_t sum = 0;
  for (int32_t i = 0; i < 1500; ++i) {
    sizes.push_back(i % 3 + 1);
    sum += sizes.back();
  }
  IovecBuffers buf(sizes);
  ASSERT_EQ(sum, buf.size());
  auto iov = buf.iov();
  EXPECT_EQ(sum, wrapvFull(writev, tempFile.fd(), iov.data(), iov.size()));
}

TEST_F(FileUtilTest, preadv) {
  for (auto& p : readers_) {
    IovecBuffers buf({12, 19, 31});
    ASSERT_EQ(62, buf.size());

    auto iov = buf.iov();
    EXPECT_EQ(p.first,
              wrapvFull(p.second, 0, iov.data(), iov.size(), off_t(42)));
    if (p.first != (typeof(p.first))(-1)) {
      EXPECT_EQ(in_.substr(0, p.first), buf.join().substr(0, p.first));
    }
  }
}

TEST(String, readFile) {
  srand(time(nullptr));
  const string tmpPrefix = to<string>("/tmp/folly-file-util-test-",
                                      getpid(), "-", rand(), "-");
  const string afile = tmpPrefix + "myfile";
  const string emptyFile = tmpPrefix + "myfile2";

  SCOPE_EXIT {
    unlink(afile.c_str());
    unlink(emptyFile.c_str());
  };

  EXPECT_TRUE(writeFile(string(), emptyFile.c_str()));
  EXPECT_TRUE(writeFile(StringPiece("bar"), afile.c_str()));

  {
    string contents;
    EXPECT_TRUE(readFile(emptyFile.c_str(), contents));
    EXPECT_EQ(contents, "");
    EXPECT_TRUE(readFile(afile.c_str(), contents, 0));
    EXPECT_EQ("", contents);
    EXPECT_TRUE(readFile(afile.c_str(), contents, 2));
    EXPECT_EQ("ba", contents);
    EXPECT_TRUE(readFile(afile.c_str(), contents));
    EXPECT_EQ("bar", contents);
  }
  {
    vector<unsigned char> contents;
    EXPECT_TRUE(readFile(emptyFile.c_str(), contents));
    EXPECT_EQ(vector<unsigned char>(), contents);
    EXPECT_TRUE(readFile(afile.c_str(), contents, 0));
    EXPECT_EQ(vector<unsigned char>(), contents);
    EXPECT_TRUE(readFile(afile.c_str(), contents, 2));
    EXPECT_EQ(vector<unsigned char>({'b', 'a'}), contents);
    EXPECT_TRUE(readFile(afile.c_str(), contents));
    EXPECT_EQ(vector<unsigned char>({'b', 'a', 'r'}), contents);
  }
}

}}  // namespaces
