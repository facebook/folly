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

#include "folly/experimental/File.h"

#include <system_error>

#include <glog/logging.h>

namespace folly {

File::File(const char* name, int flags, mode_t mode)
  : fd_(::open(name, flags, mode)), ownsFd_(false) {
  if (fd_ == -1) {
    throw std::system_error(errno, std::system_category(), "open() failed");
  }
  ownsFd_ = true;
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

