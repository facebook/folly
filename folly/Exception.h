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

#ifndef FOLLY_EXCEPTION_H_
#define FOLLY_EXCEPTION_H_

#include <errno.h>

#include <stdexcept>
#include <system_error>

#include "folly/Conv.h"
#include "folly/Likely.h"

namespace folly {

// Helper to throw std::system_error
void throwSystemError(int err, const char* msg) __attribute__((noreturn));
inline void throwSystemError(int err, const char* msg) {
  throw std::system_error(err, std::system_category(), msg);
}

// Helper to throw std::system_error from errno
void throwSystemError(const char* msg) __attribute__((noreturn));
inline void throwSystemError(const char* msg) {
  throwSystemError(errno, msg);
}

// Helper to throw std::system_error from errno and components of a string
template <class... Args>
void throwSystemError(Args... args) __attribute__((noreturn));
template <class... Args>
inline void throwSystemError(Args... args) {
  throwSystemError(errno, folly::to<std::string>(args...));
}

// Check a Posix return code (0 on success, error number on error), throw
// on error.
inline void checkPosixError(int err, const char* msg) {
  if (UNLIKELY(err != 0)) {
    throwSystemError(err, msg);
  }
}

// Check a Linux kernel-style return code (>= 0 on success, negative error
// number on error), throw on error.
inline void checkKernelError(ssize_t ret, const char* msg) {
  if (UNLIKELY(ret < 0)) {
    throwSystemError(-ret, msg);
  }
}

// Check a traditional Unix return code (-1 and sets errno on error), throw
// on error.
inline void checkUnixError(ssize_t ret, const char* msg) {
  if (UNLIKELY(ret == -1)) {
    throwSystemError(msg);
  }
}
inline void checkUnixError(ssize_t ret, int savedErrno, const char* msg) {
  if (UNLIKELY(ret == -1)) {
    throwSystemError(savedErrno, msg);
  }
}

}  // namespace folly

#endif /* FOLLY_EXCEPTION_H_ */

