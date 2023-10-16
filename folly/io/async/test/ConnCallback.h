/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/test/CallbackStateEnum.h>

namespace folly::test {

using VoidCallback = std::function<void()>;

class ConnCallback : public folly::AsyncSocket::ConnectCallback {
 public:
  ConnCallback()
      : state(STATE_WAITING),
        exception(folly::AsyncSocketException::UNKNOWN, "none") {}

  void connectSuccess() noexcept override {
    state = STATE_SUCCEEDED;
    if (successCallback) {
      successCallback();
    }
  }

  void connectErr(const folly::AsyncSocketException& ex) noexcept override {
    state = STATE_FAILED;
    exception = ex;
    if (errorCallback) {
      errorCallback();
    }
  }

  StateEnum state{STATE_WAITING};
  folly::AsyncSocketException exception;
  VoidCallback successCallback;
  VoidCallback errorCallback;
};

} // namespace folly::test
