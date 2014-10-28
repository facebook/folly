/*
 * Copyright 2014 Facebook, Inc.
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

#include <folly/wangle/Future.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/ExceptionWrapper.h>
#include <glog/logging.h>
#include <thrift/lib/cpp/async/TAsyncTransport.h>

namespace folly { namespace wangle {

template <class In, class Out>
class ChannelHandlerContext {
 public:
  virtual ~ChannelHandlerContext() {}

  virtual void fireRead(In msg) = 0;
  virtual void fireReadEOF() = 0;
  virtual void fireReadException(exception_wrapper e) = 0;

  virtual Future<void> fireWrite(Out msg) = 0;
  virtual Future<void> fireClose() = 0;

  virtual std::shared_ptr<AsyncTransport> getTransport() = 0;

  virtual void setWriteFlags(WriteFlags flags) = 0;
  virtual WriteFlags getWriteFlags() = 0;

  virtual void setReadBufferSettings(
      uint64_t minAvailable,
      uint64_t allocationSize) = 0;
  virtual std::pair<uint64_t, uint64_t> getReadBufferSettings() = 0;
};

template <class Out>
class OutboundChannelHandlerContext {
 public:
  virtual ~OutboundChannelHandlerContext() {}
  virtual Future<void> write(Out msg) = 0;
  virtual Future<void> close() = 0;
};

template <class... Handlers>
class ChannelPipeline;

template <>
class ChannelPipeline<> : public DelayedDestruction {
 public:
  void setWriteFlags(WriteFlags flags) {
    writeFlags_ = flags;
  }

  WriteFlags getWriteFlags() {
    return writeFlags_;
  }

  void setReadBufferSettings(uint64_t minAvailable, uint64_t allocationSize) {
    readBufferSettings_ = std::make_pair(minAvailable, allocationSize);
  }

  std::pair<uint64_t, uint64_t> getReadBufferSettings() {
    return readBufferSettings_;
  }

 protected:
  static const bool is_end{true};
  typedef void LastHandler;
  typedef void OutboundContext;

  std::shared_ptr<AsyncTransport> transport_;
  WriteFlags writeFlags_{WriteFlags::NONE};
  std::pair<uint64_t, uint64_t> readBufferSettings_{2048, 2048};

  ~ChannelPipeline() {}

  template <class T>
  void read(T&& msg) {
    LOG(FATAL) << "impossibru";
  }

  void readEOF() {
    LOG(FATAL) << "impossibru";
  }

  void readException(exception_wrapper e) {
    LOG(FATAL) << "impossibru";
  }

  template <class T>
  Future<void> write(T&& msg) {
    LOG(FATAL) << "impossibru";
    return makeFuture();
  }

  Future<void> close() {
    LOG(FATAL) << "impossibru";
    return makeFuture();
  }

  void attachPipeline() {}

  void attachTransport(
      std::shared_ptr<AsyncTransport> transport) {
    transport_ = std::move(transport);
  }

  void detachTransport() {
    transport_ = nullptr;
  }

  template <class T>
  void setOutboundContext(T ctx) {}

  template <class H>
  H* getHandler(size_t i) {
    LOG(FATAL) << "impossibru";
  }
};

template <class Handler, class... Handlers>
class ChannelPipeline<Handler, Handlers...>
  : public ChannelPipeline<Handlers...> {
 protected:
  typedef typename std::conditional<
      ChannelPipeline<Handlers...>::is_end,
      Handler,
      typename ChannelPipeline<Handlers...>::LastHandler>::type
    LastHandler;

 public:
  template <class HandlerArg, class... HandlersArgs>
  ChannelPipeline(HandlerArg&& handlerArg, HandlersArgs&&... handlersArgs)
    : ChannelPipeline<Handlers...>(std::forward<HandlersArgs>(handlersArgs)...),
      handler_(std::forward<HandlerArg>(handlerArg)),
      ctx_(this) {
    handler_.attachPipeline(&ctx_);
    ChannelPipeline<Handlers...>::setOutboundContext(&ctx_);
  }

  ~ChannelPipeline() {}

  void destroy() override {
    handler_.detachPipeline(&ctx_);
  }

  void read(typename Handler::rin msg) {
    ChannelPipeline<>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    handler_.read(&ctx_, std::forward<typename Handler::rin>(msg));
  }

  void readEOF() {
    ChannelPipeline<>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    handler_.readEOF(&ctx_);
  }

  void readException(exception_wrapper e) {
    ChannelPipeline<>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    handler_.readException(&ctx_, std::move(e));
  }

  Future<void> write(typename LastHandler::win msg) {
    ChannelPipeline<>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    return ChannelPipeline<LastHandler>::writeHere(
        std::forward<typename LastHandler::win>(msg));
  }

  Future<void> close() {
    ChannelPipeline<>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    return ChannelPipeline<LastHandler>::closeHere();
  }

  void attachTransport(
      std::shared_ptr<AsyncTransport> transport) {
    ChannelPipeline<>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    CHECK(!ChannelPipeline<>::transport_);
    ChannelPipeline<Handlers...>::attachTransport(std::move(transport));
    handler_.attachTransport(&ctx_);
  }

  void detachTransport() {
    ChannelPipeline<>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    ChannelPipeline<Handlers...>::detachTransport();
    handler_.detachTransport(&ctx_);
  }

  std::shared_ptr<AsyncTransport> getTransport() {
    return ChannelPipeline<>::transport_;
  }

  template <class H>
  H* getHandler(size_t i) {
    if (i == 0) {
      auto ptr = dynamic_cast<H*>(&handler_);
      CHECK(ptr);
      return ptr;
    } else {
      return ChannelPipeline<Handlers...>::template getHandler<H>(i-1);
    }
  }

 protected:
  static const bool is_end{false};

  typedef OutboundChannelHandlerContext<typename Handler::wout> OutboundContext;

  void setOutboundContext(OutboundContext* ctx) {
    outboundCtx_ = ctx;
  }

  Future<void> writeHere(typename Handler::win msg) {
    return handler_.write(&ctx_, std::forward<typename Handler::win>(msg));
  }

  Future<void> closeHere() {
    return handler_.close(&ctx_);
  }

 private:
  class Context
    : public ChannelHandlerContext<typename Handler::rout,
                                   typename Handler::wout>,
      public OutboundChannelHandlerContext<typename Handler::win> {
   public:
    explicit Context(ChannelPipeline* pipeline) : pipeline_(pipeline) {}
    ChannelPipeline* pipeline_;

    void fireRead(typename Handler::rout msg) override {
      ChannelPipeline<>::DestructorGuard dg(pipeline_);
      pipeline_->fireRead(std::forward<typename Handler::rout>(msg));
    }

    void fireReadEOF() override {
      ChannelPipeline<>::DestructorGuard dg(pipeline_);
      return pipeline_->fireReadEOF();
    }

    void fireReadException(exception_wrapper e) override {
      ChannelPipeline<>::DestructorGuard dg(pipeline_);
      return pipeline_->fireReadException(std::move(e));
    }

    Future<void> fireWrite(typename Handler::wout msg) override {
      ChannelPipeline<>::DestructorGuard dg(pipeline_);
      return pipeline_->fireWrite(std::forward<typename Handler::wout>(msg));
    }

    Future<void> write(typename Handler::win msg) override {
      ChannelPipeline<>::DestructorGuard dg(pipeline_);
      return pipeline_->writeHere(std::forward<typename Handler::win>(msg));
    }

    Future<void> fireClose() override {
      ChannelPipeline<>::DestructorGuard dg(pipeline_);
      return pipeline_->fireClose();
    }

    Future<void> close() override {
      ChannelPipeline<>::DestructorGuard dg(pipeline_);
      return pipeline_->closeHere();
    }

    std::shared_ptr<AsyncTransport> getTransport() override {
      return pipeline_->transport_;
    }

    void setWriteFlags(WriteFlags flags) override {
      pipeline_->setWriteFlags(flags);
    }

    WriteFlags getWriteFlags() override {
      return pipeline_->getWriteFlags();
    }

    void setReadBufferSettings(
        uint64_t minAvailable,
        uint64_t allocationSize) override {
      pipeline_->setReadBufferSettings(minAvailable, allocationSize);
    }

    std::pair<uint64_t, uint64_t> getReadBufferSettings() override {
      return pipeline_->getReadBufferSettings();
    }
  };

  void fireRead(typename Handler::rout msg) {
    if (!ChannelPipeline<Handlers...>::is_end) {
      ChannelPipeline<Handlers...>::read(
          std::forward<typename Handler::rout>(msg));
    } else {
      LOG(WARNING) << "read() reached end of pipeline";
    }
  }

  void fireReadEOF() {
    if (!ChannelPipeline<Handlers...>::is_end) {
      ChannelPipeline<Handlers...>::readEOF();
    } else {
      LOG(WARNING) << "readEOF() reached end of pipeline";
    }
  }

  void fireReadException(exception_wrapper e) {
    if (!ChannelPipeline<Handlers...>::is_end) {
      ChannelPipeline<Handlers...>::readException(std::move(e));
    } else {
      LOG(WARNING) << "readException() reached end of pipeline";
    }
  }

  Future<void> fireWrite(typename Handler::wout msg) {
    if (outboundCtx_) {
      return outboundCtx_->write(std::forward<typename Handler::wout>(msg));
    } else {
      LOG(WARNING) << "write() reached end of pipeline";
      return makeFuture();
    }
  }

  Future<void> fireClose() {
    if (outboundCtx_) {
      return outboundCtx_->close();
    } else {
      LOG(WARNING) << "close() reached end of pipeline";
      return makeFuture();
    }
  }

  friend class Context;
  Handler handler_;
  Context ctx_;
  OutboundContext* outboundCtx_{nullptr};
};

}}

namespace folly {

class AsyncSocket;

template <typename Pipeline>
class PipelineFactory {
 public:
  virtual Pipeline* newPipeline(std::shared_ptr<AsyncSocket>) = 0;
  virtual ~PipelineFactory() {}
};

}
