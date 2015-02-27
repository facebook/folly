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

#pragma once

#include <folly/Format.h>
#include <folly/io/async/DelayedDestruction.h>

namespace folly {

class AsyncSocketException : public std::runtime_error {
 public:
  enum AsyncSocketExceptionType
  { UNKNOWN = 0
  , NOT_OPEN = 1
  , ALREADY_OPEN = 2
  , TIMED_OUT = 3
  , END_OF_FILE = 4
  , INTERRUPTED = 5
  , BAD_ARGS = 6
  , CORRUPTED_DATA = 7
  , INTERNAL_ERROR = 8
  , NOT_SUPPORTED = 9
  , INVALID_STATE = 10
  , SSL_ERROR = 12
  , COULD_NOT_BIND = 13
  , SASL_HANDSHAKE_TIMEOUT = 14
  };

  AsyncSocketException(
    AsyncSocketExceptionType type, const std::string& message) :
      std::runtime_error(message),
      type_(type), errno_(0) {}

  /** Error code */
  AsyncSocketExceptionType type_;

  /** A copy of the errno. */
  int errno_;

  AsyncSocketException(AsyncSocketExceptionType type,
                      const std::string& message,
                      int errno_copy) :
      std::runtime_error(getMessage(message, errno_copy)),
      type_(type), errno_(errno_copy) {}

  AsyncSocketExceptionType getType() const noexcept { return type_; }
  int getErrno() const noexcept { return errno_; }

 protected:
  /** Just like strerror_r but returns a C++ string object. */
  std::string strerror_s(int errno_copy) {
    return "errno = " + folly::to<std::string>(errno_copy);
  }

  /** Return a message based on the input. */
  std::string getMessage(const std::string &message,
                                int errno_copy) {
    if (errno_copy != 0) {
      return message + ": " + strerror_s(errno_copy);
    } else {
      return message;
    }
  }
};

} // folly
