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

#include "folly/File.h"
#include "folly/Format.h"
#include "folly/ScopeGuard.h"

#include <system_error>

#include <glog/logging.h>

namespace folly {

File::File()
  : fd_(-1)
  , ownsFd_(false)
{}

File::File(int fd, bool ownsFd)
  : fd_(fd)
  , ownsFd_(ownsFd)
{}

File::File(const char* name, int flags, mode_t mode)
  : fd_(::open(name, flags, mode))
  , ownsFd_(false) {

  if (fd_ < 0) {
    throw std::system_error(errno, std::system_category(),
                            folly::format("open(\"{}\", {:#o}, 0{:#o}) failed",
                                          name, flags, mode).str());
  }
  ownsFd_ = true;
}

File::File(File&& other)
  : fd_(other.fd_)
  , ownsFd_(other.ownsFd_) {

  other.release();
}

File& File::operator=(File&& other) {
  closeNoThrow();
  swap(other);
  return *this;
}

File::~File() {
  closeNoThrow();  // ignore error
}

/* static */ File File::temporary() {
  // make a temp file with tmpfile(), dup the fd, then return it in a File.
  FILE* tmpFile = tmpfile();
  if (!tmpFile) {
    throw std::system_error(errno, std::system_category(), "tmpfile() failed");
  }
  SCOPE_EXIT { fclose(tmpFile); };

  int fd = dup(fileno(tmpFile));
  if (fd < 0) {
    throw std::system_error(errno, std::system_category(), "dup() failed");
  }

  return File(fd, true);
}

void File::release() {
  fd_ = -1;
  ownsFd_ = false;
}

void File::swap(File& other) {
  using std::swap;
  swap(fd_, other.fd_);
  swap(ownsFd_, other.ownsFd_);
}

void swap(File& a, File& b) {
  a.swap(b);
}

void File::close() {
  if (!closeNoThrow()) {
    throw std::system_error(errno, std::system_category(), "close() failed");
  }
}

bool File::closeNoThrow() {
  int r = ownsFd_ ? ::close(fd_) : 0;
  release();
  return r == 0;
}

}  // namespace folly
