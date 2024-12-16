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

#include <folly/Portability.h>

#include <functional>

#include <folly/io/coro/Transport.h>
#include <folly/io/coro/TransportCallbacks.h>

#if FOLLY_HAS_COROUTINES

using namespace folly::coro;

namespace folly {
namespace coro {

Task<Transport> Transport::newConnectedSocket(
    folly::EventBase* evb,
    const folly::SocketAddress& destAddr,
    std::chrono::milliseconds connectTimeout,
    const SocketOptionMap& options,
    const SocketAddress& bindAddr,
    const std::string& ifName) {
  auto socket = AsyncSocket::newSocket(evb);

  socket->setReadCB(nullptr);
  ConnectCallback cb{*socket};
  socket->connect(
      &cb, destAddr, connectTimeout.count(), options, bindAddr, ifName);
  auto waitRet = co_await co_awaitTry(cb.wait());
  if (waitRet.hasException()) {
    co_yield co_error(std::move(waitRet.exception()));
  }
  if (cb.error()) {
    co_yield co_error(std::move(cb.error()));
  }
  co_return Transport(evb, std::move(socket));
}

Task<size_t> Transport::read(
    folly::MutableByteRange buf, std::chrono::milliseconds timeout) {
  if (deferredReadEOF_) {
    deferredReadEOF_ = false;
    co_return 0;
  }
  VLOG(5) << "Transport::read(), expecting max len " << buf.size();

  ReadCallback cb{eventBase_->timer(), *transport_, buf, timeout};
  transport_->setReadCB(&cb);
  auto waitRet = co_await co_awaitTry(cb.wait());
  if (cb.error()) {
    co_yield co_error(std::move(cb.error()));
  }
  if (waitRet.hasException() &&
      (!waitRet.tryGetExceptionObject<OperationCancelled>() ||
       (!cb.eof && cb.length == 0))) {
    // Got a non-cancel exception, or cancel with nothing read
    co_yield co_error(std::move(waitRet.exception()));
  }
  transport_->setReadCB(nullptr);
  deferredReadEOF_ = (cb.eof && cb.length > 0);
  co_return cb.length;
}

Task<size_t> Transport::read(
    folly::IOBufQueue& readBuf,
    std::size_t minReadSize,
    std::size_t newAllocationSize,
    std::chrono::milliseconds timeout) {
  if (deferredReadEOF_) {
    deferredReadEOF_ = false;
    co_return 0;
  }
  VLOG(5) << "Transport::read(), expecting minReadSize=" << minReadSize;

  auto readBufStartLength = readBuf.chainLength();
  ReadCallback cb{
      eventBase_->timer(),
      *transport_,
      &readBuf,
      minReadSize,
      newAllocationSize,
      timeout};
  transport_->setReadCB(&cb);
  auto waitRet = co_await co_awaitTry(cb.wait());
  if (cb.error()) {
    co_yield co_error(std::move(cb.error()));
  }
  if (waitRet.hasException() &&
      (!waitRet.tryGetExceptionObject<OperationCancelled>() ||
       (!cb.eof && cb.length == 0))) {
    // Got a non-cancel exception, or cancel with nothing read
    co_yield co_error(std::move(waitRet.exception()));
  }
  transport_->setReadCB(nullptr);
  auto length = readBuf.chainLength() - readBufStartLength;
  deferredReadEOF_ = (cb.eof && length > 0);
  co_return length;
}

Task<folly::Unit> Transport::write(
    folly::ByteRange buf,
    std::chrono::milliseconds timeout,
    folly::WriteFlags writeFlags,
    WriteInfo* writeInfo) {
  transport_->setSendTimeout(timeout.count());
  WriteCallback cb{*transport_};
  transport_->write(&cb, buf.begin(), buf.size(), writeFlags);
  auto waitRet = co_await co_awaitTry(cb.wait());
  if (waitRet.hasException()) {
    if (writeInfo) {
      writeInfo->bytesWritten = cb.bytesWritten;
    }
    co_yield co_error(std::move(waitRet.exception()));
  }

  if (cb.error) {
    if (writeInfo) {
      writeInfo->bytesWritten = cb.bytesWritten;
    }
    co_yield co_error(std::move(*cb.error));
  }
  co_return unit;
}

Task<folly::Unit> Transport::write(
    folly::IOBufQueue& ioBufQueue,
    std::chrono::milliseconds timeout,
    folly::WriteFlags writeFlags,
    WriteInfo* writeInfo) {
  transport_->setSendTimeout(timeout.count());
  WriteCallback cb{*transport_};
  auto iovec = ioBufQueue.front()->getIov();
  transport_->writev(&cb, iovec.data(), iovec.size(), writeFlags);
  auto waitRet = co_await co_awaitTry(cb.wait());
  if (waitRet.hasException()) {
    if (writeInfo) {
      writeInfo->bytesWritten = cb.bytesWritten;
    }
    co_yield co_error(std::move(waitRet.exception()));
  }

  if (cb.error) {
    if (writeInfo) {
      writeInfo->bytesWritten = cb.bytesWritten;
    }
    co_yield co_error(std::move(*cb.error));
  }
  co_return unit;
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
