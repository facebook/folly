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

#ifndef FOLLY_FILE_H_
#error This file may only be included from folly/experimental/File.h
#endif

#include <algorithm>

namespace folly {

inline File::File(int fd, bool ownsFd) : fd_(fd), ownsFd_(ownsFd) { }

inline File::~File() {
  closeNoThrow();  // ignore error
}

inline void File::release() {
  fd_ = -1;
  ownsFd_ = false;
}

inline void File::swap(File& other) {
  using std::swap;
  swap(fd_, other.fd_);
  swap(ownsFd_, other.ownsFd_);
}

inline File::File(File&& other) : fd_(other.fd_), ownsFd_(other.ownsFd_) {
  other.release();
}

inline File& File::operator=(File&& other) {
  File(std::move(other)).swap(*this);
  return *this;
}

inline void swap(File& a, File& b) {
  a.swap(b);
}

}  // namespace folly

