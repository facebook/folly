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

#include <memory>

#include <folly/SocketAddress.h>
#include <folly/futures/Future.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/IoUringBase.h>
#include <folly/net/NetworkSocket.h>

namespace folly {

class EventBase;
class IoUringBackend;

class IoUringRecvCallback {
 public:
  virtual ~IoUringRecvCallback() = default;

  virtual void recvSuccess(std::unique_ptr<IOBuf> data) = 0;
  virtual void recvEOF() noexcept = 0;
  virtual void recvErr(
      int err,
      std::unique_ptr<const AsyncSocketException> exception) noexcept = 0;
};

class IoUringRecvHandle : public DelayedDestruction {
 public:
  using UniquePtr = std::unique_ptr<IoUringRecvHandle, Destructor>;
  using PendingRead = SemiFuture<std::unique_ptr<IOBuf>>;
  using PendingReadMaybe = Optional<PendingRead>;

  static constexpr size_t kSmallRecvSize = 4096;

  static UniquePtr create(
      EventBase* evb,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringRecvCallback* callback);

  static UniquePtr clone(
      EventBase* evb,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringRecvCallback* callback,
      UniquePtr old);

  bool update(uint16_t eventFlags);
  void submit(size_t maxSize);

  bool hasQueuedData();
  std::unique_ptr<IOBuf> getQueuedData();

  void detachEventBase();
  void cancel();

 private:
  explicit IoUringRecvHandle(
      IoUringBackend* backend,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringRecvCallback* callback);

  explicit IoUringRecvHandle(
      EventBase* evb,
      IoUringBackend* backend,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringRecvCallback* callback,
      UniquePtr other);

  void setPendingRead(EventBase* evb, PendingRead&& future);
  void processPendingRead();

  class RecvRequest;
  void onRecvComplete(std::unique_ptr<IOBuf> data);
  void onEnobufs();
  void onRecvEOF();
  void onRecvErr(int err);

  IoUringBackend* backend_;
  IoUringRecvCallback* recvCallback_;
  std::unique_ptr<RecvRequest, DelayedDestruction::Destructor> request_;

  std::unique_ptr<IOBuf> queuedReceivedData_;
  bool readEnabled_{false};
  PendingReadMaybe pendingRead_{folly::none};
};

} // namespace folly
