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

#include <folly/experimental/TestUtil.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <boost/regex.hpp>
#include <folly/Conv.h>
#include <folly/Exception.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/String.h>

namespace folly {
namespace test {

namespace {

fs::path generateUniquePath(fs::path path, StringPiece namePrefix) {
  if (path.empty()) {
    path = fs::temp_directory_path();
  }
  if (namePrefix.empty()) {
    path /= fs::unique_path();
  } else {
    path /= fs::unique_path(
        to<std::string>(namePrefix, ".%%%%-%%%%-%%%%-%%%%"));
  }
  return path;
}

}  // namespace

TemporaryFile::TemporaryFile(StringPiece namePrefix,
                             fs::path dir,
                             Scope scope,
                             bool closeOnDestruction)
  : scope_(scope),
    closeOnDestruction_(closeOnDestruction),
    fd_(-1),
    path_(generateUniquePath(std::move(dir), namePrefix)) {
  fd_ = open(path_.string().c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
  checkUnixError(fd_, "open failed");

  if (scope_ == Scope::UNLINK_IMMEDIATELY) {
    boost::system::error_code ec;
    fs::remove(path_, ec);
    if (ec) {
      LOG(WARNING) << "unlink on construction failed: " << ec;
    } else {
      path_.clear();
    }
  }
}

const fs::path& TemporaryFile::path() const {
  CHECK(scope_ != Scope::UNLINK_IMMEDIATELY);
  DCHECK(!path_.empty());
  return path_;
}

TemporaryFile::~TemporaryFile() {
  if (fd_ != -1 && closeOnDestruction_) {
    if (close(fd_) == -1) {
      PLOG(ERROR) << "close failed";
    }
  }

  // If we previously failed to unlink() (UNLINK_IMMEDIATELY), we'll
  // try again here.
  if (scope_ != Scope::PERMANENT && !path_.empty()) {
    boost::system::error_code ec;
    fs::remove(path_, ec);
    if (ec) {
      LOG(WARNING) << "unlink on destruction failed: " << ec;
    }
  }
}

TemporaryDirectory::TemporaryDirectory(StringPiece namePrefix,
                                       fs::path dir,
                                       Scope scope)
  : scope_(scope),
    path_(generateUniquePath(std::move(dir), namePrefix)) {
  fs::create_directory(path_);
}

TemporaryDirectory::~TemporaryDirectory() {
  if (scope_ == Scope::DELETE_ON_DESTRUCTION) {
    boost::system::error_code ec;
    fs::remove_all(path_, ec);
    if (ec) {
      LOG(WARNING) << "recursive delete on destruction failed: " << ec;
    }
  }
}

ChangeToTempDir::ChangeToTempDir() : initialPath_(fs::current_path()) {
  std::string p = dir_.path().string();
  ::chdir(p.c_str());
}

ChangeToTempDir::~ChangeToTempDir() {
  std::string p = initialPath_.string();
  ::chdir(p.c_str());
}

namespace detail {

bool hasPCREPatternMatch(StringPiece pattern, StringPiece target) {
  return boost::regex_match(
    target.begin(),
    target.end(),
    boost::regex(pattern.begin(), pattern.end())
  );
}

bool hasNoPCREPatternMatch(StringPiece pattern, StringPiece target) {
  return !hasPCREPatternMatch(pattern, target);
}

}  // namespace detail

CaptureFD::CaptureFD(int fd) : fd_(fd), readOffset_(0) {
  oldFDCopy_ = dup(fd_);
  PCHECK(oldFDCopy_ != -1) << "Could not copy FD " << fd_;

  int file_fd = open(file_.path().string().c_str(), O_WRONLY|O_CREAT, 0600);
  PCHECK(dup2(file_fd, fd_) != -1) << "Could not replace FD " << fd_
    << " with " << file_fd;
  PCHECK(close(file_fd) != -1) << "Could not close " << file_fd;
}

void CaptureFD::release() {
  if (oldFDCopy_ != fd_) {
    PCHECK(dup2(oldFDCopy_, fd_) != -1) << "Could not restore old FD "
      << oldFDCopy_ << " into " << fd_;
    PCHECK(close(oldFDCopy_) != -1) << "Could not close " << oldFDCopy_;
    oldFDCopy_ = fd_;  // Make this call idempotent
  }
}

CaptureFD::~CaptureFD() {
  release();
}

std::string CaptureFD::read() {
  std::string contents;
  std::string filename = file_.path().string();
  PCHECK(folly::readFile(filename.c_str(), contents));
  return contents;
}

std::string CaptureFD::readIncremental() {
  std::string filename = file_.path().string();
  // Yes, I know that I could just keep the file open instead. So sue me.
  folly::File f(openNoInt(filename.c_str(), O_RDONLY), true);
  auto size = lseek(f.fd(), 0, SEEK_END) - readOffset_;
  std::unique_ptr<char[]> buf(new char[size]);
  auto bytes_read = folly::preadFull(f.fd(), buf.get(), size, readOffset_);
  PCHECK(size == bytes_read);
  readOffset_ += size;
  return std::string(buf.get(), size);
}

#ifndef _MSC_VER
static std::map<std::string, std::string> getEnvVarMap() {
  std::map<std::string, std::string> data;
  for (auto it = environ; *it != nullptr; ++it) {
    std::string key, value;
    split("=", *it, key, value);
    if (key.empty()) {
      continue;
    }
    CHECK(!data.count(key)) << "already contains: " << key;
    data.emplace(move(key), move(value));
  }
  return data;
}

EnvVarSaver::EnvVarSaver() {
  saved_ = getEnvVarMap();
}

EnvVarSaver::~EnvVarSaver() {
  for (const auto& kvp : getEnvVarMap()) {
    if (saved_.count(kvp.first)) {
      continue;
    }
    PCHECK(0 == unsetenv(kvp.first.c_str()));
  }
  for (const auto& kvp : saved_) {
    PCHECK(0 == setenv(kvp.first.c_str(), kvp.second.c_str(), (int)true));
  }
}
#endif

}  // namespace test
}  // namespace folly
