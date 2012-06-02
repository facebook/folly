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

#include "folly/experimental/TestUtil.h"

#include <stdlib.h>
#include <errno.h>
#include <stdexcept>
#include <system_error>

#include "folly/Format.h"

namespace folly {
namespace test {

TemporaryFile::TemporaryFile(const char* prefix, Scope scope,
                             bool closeOnDestruction)
  : scope_(scope),
    closeOnDestruction_(closeOnDestruction) {
  static const char* suffix = ".XXXXXX";  // per mkstemp(3)
  if (!prefix || prefix[0] == '\0') {
    prefix = "temp";
  }
  const char* dir = nullptr;
  if (!strchr(prefix, '/')) {
    // Not a full path, try getenv("TMPDIR") or "/tmp"
    dir = getenv("TMPDIR");
    if (!dir) {
      dir = "/tmp";
    }
    // The "!" is a placeholder to ensure that &(path[0]) is null-terminated.
    // This is the only standard-compliant way to get at a null-terminated
    // non-const char string inside a std::string: put the null-terminator
    // yourself.
    path_ = format("{}/{}{}!", dir, prefix, suffix).str();
  } else {
    path_ = format("{}{}!", prefix, suffix).str();
  }

  // Replace the '!' with a null terminator, we'll get rid of it later
  path_[path_.size() - 1] = '\0';

  fd_ = mkstemp(&(path_[0]));
  if (fd_ == -1) {
    throw std::system_error(errno, std::system_category(),
                            format("mkstemp failed: {}", path_).str().c_str());
  }

  DCHECK_EQ(path_[path_.size() - 1], '\0');
  path_.erase(path_.size() - 1);

  if (scope_ == Scope::UNLINK_IMMEDIATELY) {
    if (unlink(path_.c_str()) == -1) {
      throw std::system_error(errno, std::system_category(),
                              format("unlink failed: {}", path_).str().c_str());
    }
    path_.clear();  // path no longer available or meaningful
  }
}

const std::string& TemporaryFile::path() const {
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
    if (unlink(path_.c_str()) == -1) {
      PLOG(ERROR) << "unlink(" << path_ << ") failed";
    }
  }
}

}  // namespace test
}  // namespace folly
