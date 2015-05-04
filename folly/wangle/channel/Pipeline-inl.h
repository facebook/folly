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

#include <glog/logging.h>

namespace folly { namespace wangle {

template <class R, class W>
Pipeline<R, W>::Pipeline() : isStatic_(false) {}

template <class R, class W>
Pipeline<R, W>::Pipeline(bool isStatic) : isStatic_(isStatic) {
  CHECK(isStatic_);
}

template <class R, class W>
Pipeline<R, W>::~Pipeline() {
  if (!isStatic_) {
    detachHandlers();
  }
}

template <class R, class W>
std::shared_ptr<AsyncTransport> Pipeline<R, W>::getTransport() {
  return transport_;
}

template <class R, class W>
void Pipeline<R, W>::setWriteFlags(WriteFlags flags) {
  writeFlags_ = flags;
}

template <class R, class W>
WriteFlags Pipeline<R, W>::getWriteFlags() {
  return writeFlags_;
}

template <class R, class W>
void Pipeline<R, W>::setReadBufferSettings(
    uint64_t minAvailable,
    uint64_t allocationSize) {
  readBufferSettings_ = std::make_pair(minAvailable, allocationSize);
}

template <class R, class W>
std::pair<uint64_t, uint64_t> Pipeline<R, W>::getReadBufferSettings() {
  return readBufferSettings_;
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value>::type
Pipeline<R, W>::read(R msg) {
  if (!front_) {
    throw std::invalid_argument("read(): no inbound handler in Pipeline");
  }
  front_->read(std::forward<R>(msg));
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value>::type
Pipeline<R, W>::readEOF() {
  if (!front_) {
    throw std::invalid_argument("readEOF(): no inbound handler in Pipeline");
  }
  front_->readEOF();
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value>::type
Pipeline<R, W>::readException(exception_wrapper e) {
  if (!front_) {
    throw std::invalid_argument(
        "readException(): no inbound handler in Pipeline");
  }
  front_->readException(std::move(e));
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value, Future<void>>::type
Pipeline<R, W>::write(W msg) {
  if (!back_) {
    throw std::invalid_argument("write(): no outbound handler in Pipeline");
  }
  return back_->write(std::forward<W>(msg));
}

template <class R, class W>
template <class T>
typename std::enable_if<!std::is_same<T, Nothing>::value, Future<void>>::type
Pipeline<R, W>::close() {
  if (!back_) {
    throw std::invalid_argument("close(): no outbound handler in Pipeline");
  }
  return back_->close();
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addBack(std::shared_ptr<H> handler) {
  typedef typename ContextType<H, Pipeline<R, W>>::type Context;
  return addHelper(std::make_shared<Context>(this, std::move(handler)), false);
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addBack(H&& handler) {
  return addBack(std::make_shared<H>(std::forward<H>(handler)));
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addBack(H* handler) {
  return addBack(std::shared_ptr<H>(handler, [](H*){}));
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addFront(std::shared_ptr<H> handler) {
  typedef typename ContextType<H, Pipeline<R, W>>::type Context;
  return addHelper(std::make_shared<Context>(this, std::move(handler)), true);
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addFront(H&& handler) {
  return addFront(std::make_shared<H>(std::forward<H>(handler)));
}

template <class R, class W>
template <class H>
Pipeline<R, W>& Pipeline<R, W>::addFront(H* handler) {
  return addFront(std::shared_ptr<H>(handler, [](H*){}));
}

template <class R, class W>
template <class H>
H* Pipeline<R, W>::getHandler(int i) {
  typedef typename ContextType<H, Pipeline<R, W>>::type Context;
  auto ctx = dynamic_cast<Context*>(ctxs_[i].get());
  CHECK(ctx);
  return ctx->getHandler();
}

namespace detail {

template <class T>
inline void logWarningIfNotNothing(const std::string& warning) {
  LOG(WARNING) << warning;
}

template <>
inline void logWarningIfNotNothing<Nothing>(const std::string& warning) {
  // do nothing
}

} // detail

// TODO Have read/write/etc check that pipeline has been finalized
template <class R, class W>
void Pipeline<R, W>::finalize() {
  if (!inCtxs_.empty()) {
    front_ = dynamic_cast<InboundLink<R>*>(inCtxs_.front());
    for (size_t i = 0; i < inCtxs_.size() - 1; i++) {
      inCtxs_[i]->setNextIn(inCtxs_[i+1]);
    }
  }

  if (!outCtxs_.empty()) {
    back_ = dynamic_cast<OutboundLink<W>*>(outCtxs_.back());
    for (size_t i = outCtxs_.size() - 1; i > 0; i--) {
      outCtxs_[i]->setNextOut(outCtxs_[i-1]);
    }
  }

  if (!front_) {
    detail::logWarningIfNotNothing<R>(
        "No inbound handler in Pipeline, inbound operations will throw "
        "std::invalid_argument");
  }
  if (!back_) {
    detail::logWarningIfNotNothing<W>(
        "No outbound handler in Pipeline, outbound operations will throw "
        "std::invalid_argument");
  }

  for (auto it = ctxs_.rbegin(); it != ctxs_.rend(); it++) {
    (*it)->attachPipeline();
  }
}

template <class R, class W>
template <class H>
bool Pipeline<R, W>::setOwner(H* handler) {
  typedef typename ContextType<H, Pipeline<R, W>>::type Context;
  for (auto& ctx : ctxs_) {
    auto ctxImpl = dynamic_cast<Context*>(ctx.get());
    if (ctxImpl && ctxImpl->getHandler() == handler) {
      owner_ = ctx;
      return true;
    }
  }
  return false;
}

template <class R, class W>
void Pipeline<R, W>::attachTransport(
    std::shared_ptr<AsyncTransport> transport) {
  transport_ = std::move(transport);
  for (auto& ctx : ctxs_) {
    ctx->attachTransport();
  }
}

template <class R, class W>
void Pipeline<R, W>::detachTransport() {
  transport_ = nullptr;
  for (auto& ctx : ctxs_) {
    ctx->detachTransport();
  }
}

template <class R, class W>
template <class Context>
void Pipeline<R, W>::addContextFront(Context* ctx) {
  addHelper(std::shared_ptr<Context>(ctx, [](Context*){}), true);
}

template <class R, class W>
void Pipeline<R, W>::detachHandlers() {
  for (auto& ctx : ctxs_) {
    if (ctx != owner_) {
      ctx->detachPipeline();
    }
  }
}

template <class R, class W>
template <class Context>
Pipeline<R, W>& Pipeline<R, W>::addHelper(
    std::shared_ptr<Context>&& ctx,
    bool front) {
  ctxs_.insert(front ? ctxs_.begin() : ctxs_.end(), ctx);
  if (Context::dir == HandlerDir::BOTH || Context::dir == HandlerDir::IN) {
    inCtxs_.insert(front ? inCtxs_.begin() : inCtxs_.end(), ctx.get());
  }
  if (Context::dir == HandlerDir::BOTH || Context::dir == HandlerDir::OUT) {
    outCtxs_.insert(front ? outCtxs_.begin() : outCtxs_.end(), ctx.get());
  }
  return *this;
}

}} // folly::wangle
