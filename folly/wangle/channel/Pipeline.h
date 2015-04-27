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

#include <folly/wangle/channel/HandlerContext.h>
#include <folly/futures/Future.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/ExceptionWrapper.h>
#include <folly/Memory.h>
#include <glog/logging.h>

namespace folly { namespace wangle {

/*
 * R is the inbound type, i.e. inbound calls start with pipeline.read(R)
 * W is the outbound type, i.e. outbound calls start with pipeline.write(W)
 */
template <class R, class W, class... Handlers>
class Pipeline;

template <class R, class W>
class Pipeline<R, W> : public DelayedDestruction {
 public:
  Pipeline() {}
  ~Pipeline() {}

  std::shared_ptr<AsyncTransport> getTransport() {
    return transport_;
  }

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

  void read(R msg) {
    front_->read(std::forward<R>(msg));
  }

  void readEOF() {
    front_->readEOF();
  }

  void readException(exception_wrapper e) {
    front_->readException(std::move(e));
  }

  Future<void> write(W msg) {
    return back_->write(std::forward<W>(msg));
  }

  Future<void> close() {
    return back_->close();
  }

  template <class H>
  Pipeline& addBack(H&& handler) {
    ctxs_.push_back(folly::make_unique<ContextImpl<Pipeline, H>>(
        this, std::forward<H>(handler)));
    return *this;
  }

  template <class H>
  Pipeline& addFront(H&& handler) {
    ctxs_.insert(
        ctxs_.begin(),
        folly::make_unique<ContextImpl<Pipeline, H>>(
            this,
            std::forward<H>(handler)));
    return *this;
  }

  template <class H>
  H* getHandler(int i) {
    auto ctx = dynamic_cast<ContextImpl<Pipeline, H>*>(ctxs_[i].get());
    CHECK(ctx);
    return ctx->getHandler();
  }

  void finalize() {
    finalizeHelper();
    InboundHandlerContext<R>* front;
    front_ = dynamic_cast<InboundHandlerContext<R>*>(
        ctxs_.front().get());
    if (!front_) {
      throw std::invalid_argument("wrong type for first handler");
    }
  }

 protected:
  explicit Pipeline(bool shouldFinalize) {
    CHECK(!shouldFinalize);
  }

  void finalizeHelper() {
    if (ctxs_.empty()) {
      return;
    }

    for (size_t i = 0; i < ctxs_.size() - 1; i++) {
      ctxs_[i]->link(ctxs_[i+1].get());
    }

    back_ = dynamic_cast<OutboundHandlerContext<W>*>(ctxs_.back().get());
    if (!back_) {
      throw std::invalid_argument("wrong type for last handler");
    }
  }

  PipelineContext* getLocalFront() {
    return ctxs_.empty() ? nullptr : ctxs_.front().get();
  }

  static const bool is_end{true};

  std::shared_ptr<AsyncTransport> transport_;
  WriteFlags writeFlags_{WriteFlags::NONE};
  std::pair<uint64_t, uint64_t> readBufferSettings_{2048, 2048};

  void attachPipeline() {}

  void attachTransport(
      std::shared_ptr<AsyncTransport> transport) {
    transport_ = std::move(transport);
  }

  void detachTransport() {
    transport_ = nullptr;
  }

  OutboundHandlerContext<W>* back_{nullptr};

 private:
  InboundHandlerContext<R>* front_{nullptr};
  std::vector<std::unique_ptr<PipelineContext>> ctxs_;
};

template <class R, class W, class Handler, class... Handlers>
class Pipeline<R, W, Handler, Handlers...>
  : public Pipeline<R, W, Handlers...> {
 protected:
  template <class HandlerArg, class... HandlersArgs>
  Pipeline(
      bool shouldFinalize,
      HandlerArg&& handlerArg,
      HandlersArgs&&... handlersArgs)
    : Pipeline<R, W, Handlers...>(
          false,
          std::forward<HandlersArgs>(handlersArgs)...),
          ctx_(this, std::forward<HandlerArg>(handlerArg)) {
    if (shouldFinalize) {
      finalize();
    }
  }

 public:
  template <class... HandlersArgs>
  explicit Pipeline(HandlersArgs&&... handlersArgs)
    : Pipeline(true, std::forward<HandlersArgs>(handlersArgs)...) {}

  ~Pipeline() {}

  void read(R msg) {
    typename Pipeline<R, W>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    front_->read(std::forward<R>(msg));
  }

  void readEOF() {
    typename Pipeline<R, W>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    front_->readEOF();
  }

  void readException(exception_wrapper e) {
    typename Pipeline<R, W>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    front_->readException(std::move(e));
  }

  Future<void> write(W msg) {
    typename Pipeline<R, W>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    return back_->write(std::forward<W>(msg));
  }

  Future<void> close() {
    typename Pipeline<R, W>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    return back_->close();
  }

  void attachTransport(
      std::shared_ptr<AsyncTransport> transport) {
    typename Pipeline<R, W>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    CHECK((!Pipeline<R, W>::transport_));
    Pipeline<R, W, Handlers...>::attachTransport(std::move(transport));
    forEachCtx([&](PipelineContext* ctx){
      ctx->attachTransport();
    });
  }

  void detachTransport() {
    typename Pipeline<R, W>::DestructorGuard dg(
        static_cast<DelayedDestruction*>(this));
    Pipeline<R, W, Handlers...>::detachTransport();
    forEachCtx([&](PipelineContext* ctx){
      ctx->detachTransport();
    });
  }

  std::shared_ptr<AsyncTransport> getTransport() {
    return Pipeline<R, W>::transport_;
  }

  template <class H>
  Pipeline& addBack(H&& handler) {
    Pipeline<R, W>::addBack(std::move(handler));
    return *this;
  }

  template <class H>
  Pipeline& addFront(H&& handler) {
    ctxs_.insert(
        ctxs_.begin(),
        folly::make_unique<ContextImpl<Pipeline, H>>(
            this,
            std::move(handler)));
    return *this;
  }

  template <class H>
  H* getHandler(size_t i) {
    if (i > ctxs_.size()) {
      return Pipeline<R, W, Handlers...>::template getHandler<H>(
          i - (ctxs_.size() + 1));
    } else {
      auto pctx = (i == ctxs_.size()) ? &ctx_ : ctxs_[i].get();
      auto ctx = dynamic_cast<ContextImpl<Pipeline, H>*>(pctx);
      return ctx->getHandler();
    }
  }

  void finalize() {
    finalizeHelper();
    auto ctx = ctxs_.empty() ? &ctx_ : ctxs_.front().get();
    front_ = dynamic_cast<InboundHandlerContext<R>*>(ctx);
    if (!front_) {
      throw std::invalid_argument("wrong type for first handler");
    }
  }

 protected:
  void finalizeHelper() {
    Pipeline<R, W, Handlers...>::finalizeHelper();
    back_ = Pipeline<R, W, Handlers...>::back_;
    if (!back_) {
      auto is_at_end = Pipeline<R, W, Handlers...>::is_end;
      CHECK(is_at_end);
      back_ = dynamic_cast<OutboundHandlerContext<W>*>(&ctx_);
      if (!back_) {
        throw std::invalid_argument("wrong type for last handler");
      }
    }

    if (!ctxs_.empty()) {
      for (size_t i = 0; i < ctxs_.size() - 1; i++) {
        ctxs_[i]->link(ctxs_[i+1].get());
      }
      ctxs_.back()->link(&ctx_);
    }

    auto nextFront = Pipeline<R, W, Handlers...>::getLocalFront();
    if (nextFront) {
      ctx_.link(nextFront);
    }
  }

  PipelineContext* getLocalFront() {
    return ctxs_.empty() ? &ctx_ : ctxs_.front().get();
  }

  static const bool is_end{false};
  InboundHandlerContext<R>* front_{nullptr};
  OutboundHandlerContext<W>* back_{nullptr};

 private:
  template <class F>
  void forEachCtx(const F& func) {
    for (auto& ctx : ctxs_) {
      func(ctx.get());
    }
    func(&ctx_);
  }

  ContextImpl<Pipeline, Handler> ctx_;
  std::vector<std::unique_ptr<PipelineContext>> ctxs_;
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
