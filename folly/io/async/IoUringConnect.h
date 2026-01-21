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

#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/IoUringBase.h>
#include <folly/net/NetworkSocket.h>

namespace folly {

class IoUringConnectCallback {
 public:
  virtual ~IoUringConnectCallback() = default;

  virtual void connectSuccess() = 0;
  virtual void connectTimeout() = 0;
};

class EventBase;
class IoUringBackend;

class IoUringConnectHandle : public IoSqeBase, public AsyncTimeout {
 private:
  struct NotPubliclyConstructible {};

 public:
  using UniquePtr = std::unique_ptr<IoUringConnectHandle>;

  static IoUringConnectHandle::UniquePtr create(
      EventBase* evb,
      NetworkSocket fd,
      IoUringConnectCallback* callback,
      std::chrono::milliseconds timeout);

  IoUringConnectHandle(
      NotPubliclyConstructible,
      EventBase* evb,
      IoUringBackend* backend,
      NetworkSocket fd,
      IoUringConnectCallback* callback);

  /*
   * IoSqeBase
   */
  void processSubmit(struct io_uring_sqe* sqe) noexcept override;
  void callback(const struct io_uring_cqe* cqe) noexcept override;
  void callbackCancelled(const io_uring_cqe*) noexcept override;

  /*
   * AsyncTimeout
   */
  void timeoutExpired() noexcept override;

  bool cancel();

 private:
  IoUringBackend* backend_;
  NetworkSocket fd_;
  IoUringConnectCallback* callback_;
};

} // namespace folly
