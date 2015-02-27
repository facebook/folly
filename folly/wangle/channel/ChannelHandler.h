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

#include <folly/futures/Future.h>
#include <folly/wangle/channel/ChannelPipeline.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>

namespace folly { namespace wangle {

template <class Rin, class Rout = Rin, class Win = Rout, class Wout = Rin>
class ChannelHandler {
 public:
  typedef Rin rin;
  typedef Rout rout;
  typedef Win win;
  typedef Wout wout;
  typedef ChannelHandlerContext<Rout, Wout> Context;
  virtual ~ChannelHandler() {}

  virtual void read(Context* ctx, Rin msg) = 0;
  virtual void readEOF(Context* ctx) {
    ctx->fireReadEOF();
  }
  virtual void readException(Context* ctx, exception_wrapper e) {
    ctx->fireReadException(std::move(e));
  }

  virtual Future<void> write(Context* ctx, Win msg) = 0;
  virtual Future<void> close(Context* ctx) {
    return ctx->fireClose();
  }

  virtual void attachPipeline(Context* ctx) {}
  virtual void attachTransport(Context* ctx) {}

  virtual void detachPipeline(Context* ctx) {}
  virtual void detachTransport(Context* ctx) {}

  /*
  // Other sorts of things we might want, all shamelessly stolen from Netty
  // inbound
  virtual void exceptionCaught(
      ChannelHandlerContext* ctx,
      exception_wrapper e) {}
  virtual void channelRegistered(ChannelHandlerContext* ctx) {}
  virtual void channelUnregistered(ChannelHandlerContext* ctx) {}
  virtual void channelActive(ChannelHandlerContext* ctx) {}
  virtual void channelInactive(ChannelHandlerContext* ctx) {}
  virtual void channelReadComplete(ChannelHandlerContext* ctx) {}
  virtual void userEventTriggered(ChannelHandlerContext* ctx, void* evt) {}
  virtual void channelWritabilityChanged(ChannelHandlerContext* ctx) {}

  // outbound
  virtual Future<void> bind(
      ChannelHandlerContext* ctx,
      SocketAddress localAddress) {}
  virtual Future<void> connect(
          ChannelHandlerContext* ctx,
          SocketAddress remoteAddress, SocketAddress localAddress) {}
  virtual Future<void> disconnect(ChannelHandlerContext* ctx) {}
  virtual Future<void> deregister(ChannelHandlerContext* ctx) {}
  virtual Future<void> read(ChannelHandlerContext* ctx) {}
  virtual void flush(ChannelHandlerContext* ctx) {}
  */
};

template <class R, class W = R>
class ChannelHandlerAdapter : public ChannelHandler<R, R, W, W> {
 public:
  typedef typename ChannelHandler<R, R, W, W>::Context Context;

  void read(Context* ctx, R msg) override {
    ctx->fireRead(std::forward<R>(msg));
  }

  Future<void> write(Context* ctx, W msg) override {
    return ctx->fireWrite(std::forward<W>(msg));
  }
};

typedef ChannelHandlerAdapter<IOBufQueue&, std::unique_ptr<IOBuf>>
BytesToBytesHandler;

template <class Handler, bool Shared = true>
class ChannelHandlerPtr : public ChannelHandler<
                                   typename Handler::rin,
                                   typename Handler::rout,
                                   typename Handler::win,
                                   typename Handler::wout> {
 public:
  typedef typename std::conditional<
    Shared,
    std::shared_ptr<Handler>,
    Handler*>::type
  HandlerPtr;

  typedef typename Handler::Context Context;

  explicit ChannelHandlerPtr(HandlerPtr handler)
    : handler_(std::move(handler)) {}

  HandlerPtr getHandler() {
    return handler_;
  }

  void setHandler(HandlerPtr handler) {
    if (handler == handler_) {
      return;
    }
    if (handler_ && ctx_) {
      handler_->detachPipeline(ctx_);
    }
    handler_ = std::move(handler);
    if (handler_ && ctx_) {
      handler_->attachPipeline(ctx_);
      if (ctx_->getTransport()) {
        handler_->attachTransport(ctx_);
      }
    }
  }

  void attachPipeline(Context* ctx) override {
    ctx_ = ctx;
    if (handler_) {
      handler_->attachPipeline(ctx_);
    }
  }

  void attachTransport(Context* ctx) override {
    ctx_ = ctx;
    if (handler_) {
      handler_->attachTransport(ctx_);
    }
  }

  void detachPipeline(Context* ctx) override {
    ctx_ = ctx;
    if (handler_) {
      handler_->detachPipeline(ctx_);
    }
  }

  void detachTransport(Context* ctx) override {
    ctx_ = ctx;
    if (handler_) {
      handler_->detachTransport(ctx_);
    }
  }

  void read(Context* ctx, typename Handler::rin msg) override {
    DCHECK(handler_);
    handler_->read(ctx, std::forward<typename Handler::rin>(msg));
  }

  void readEOF(Context* ctx) override {
    DCHECK(handler_);
    handler_->readEOF(ctx);
  }

  void readException(Context* ctx, exception_wrapper e) override {
    DCHECK(handler_);
    handler_->readException(ctx, std::move(e));
  }

  Future<void> write(Context* ctx, typename Handler::win msg) override {
    DCHECK(handler_);
    return handler_->write(ctx, std::forward<typename Handler::win>(msg));
  }

  Future<void> close(Context* ctx) override {
    DCHECK(handler_);
    return handler_->close(ctx);
  }

 private:
  Context* ctx_;
  HandlerPtr handler_;
};

}}
