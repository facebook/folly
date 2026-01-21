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

#include <folly/futures/Future.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/IoUringBase.h>
#include <folly/net/NetworkSocket.h>

namespace folly {

class IoUringSendCallback {
 public:
  virtual ~IoUringSendCallback() = default;

  virtual void sendPartial(size_t bytesWritten) = 0;
  virtual void sendDone(size_t bytesWritten) = 0;
  virtual void sendErr(int err) = 0;
  virtual void releaseIOBuf(
      std::unique_ptr<IOBuf> buf,
      AsyncWriter::ReleaseIOBufCallback* callback) = 0;
};

class EventBase;
class IoUringBackend;

class IoUringSendHandle : public DelayedDestruction {
 public:
  using UniquePtr = std::unique_ptr<IoUringSendHandle, Destructor>;

  static UniquePtr create(
      EventBase* evb,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringSendCallback* callback);

  static UniquePtr clone(EventBase* evb, IoUringSendHandle::UniquePtr other);
  void detachEventBase();

  bool update(uint16_t eventFlags);
  void write(
      AsyncWriter::WriteCallback* callback,
      const struct iovec* iov,
      size_t iovCount,
      size_t partialWritten,
      size_t bytesWritten,
      std::unique_ptr<IOBuf> data,
      WriteFlags flags);

  void failWrite(const AsyncSocketException& ex);
  void failAllWrites(const AsyncSocketException& ex);

  bool empty() { return requestHead_ == nullptr; }

 private:
  explicit IoUringSendHandle(
      EventBase* evb,
      IoUringBackend* backend,
      NetworkSocket fd,
      const SocketAddress& addr,
      IoUringSendCallback* callback);

  explicit IoUringSendHandle(
      EventBase* evb, IoUringBackend* backend, UniquePtr other);

  void trySubmit();

  class SendRequest;
  void onSendStarted();
  void onSendPartial(size_t bytesWritten);
  void onSendComplete(size_t bytesWritten);
  void onSendErr(int err);
  void onReleaseIOBuf(
      std::unique_ptr<IOBuf> data, AsyncWriter::ReleaseIOBufCallback* callback);

  EventBase* evb_;
  IoUringBackend* backend_;
  NetworkSocket fd_;
  SocketAddress addr_;
  IoUringSendCallback* sendCallback_;

  SendRequest* requestHead_{nullptr};
  SendRequest* requestTail_{nullptr};

  bool sendEnabled_{false};
  Optional<SemiFuture<int>> detachedFuture_;
};

} // namespace folly
