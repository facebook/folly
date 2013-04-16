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

#include "folly/experimental/TestUtil.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "folly/Conv.h"
#include "folly/Exception.h"

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

}  // namespce

TemporaryFile::TemporaryFile(StringPiece namePrefix,
                             fs::path dir,
                             Scope scope,
                             bool closeOnDestruction)
  : scope_(scope),
    closeOnDestruction_(closeOnDestruction),
    fd_(-1),
    path_(generateUniquePath(std::move(dir), namePrefix)) {
  fd_ = open(path_.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
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

}  // namespace test
}  // namespace folly
