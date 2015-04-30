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
template <class R, class W>
class Pipeline : public DelayedDestruction {
 public:
  Pipeline() : isStatic_(false) {}

  ~Pipeline() {
    if (!isStatic_) {
      detachHandlers();
    }
  }

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
  Pipeline& addBack(std::shared_ptr<H> handler) {
    typedef typename ContextType<H, Pipeline>::type Context;
    return addHelper(std::make_shared<Context>(this, std::move(handler)), false);
  }

  template <class H>
  Pipeline& addBack(H&& handler) {
    return addBack(std::make_shared<H>(std::forward<H>(handler)));
  }

  template <class H>
  Pipeline& addBack(H* handler) {
    return addBack(std::shared_ptr<H>(handler, [](H*){}));
  }

  template <class H>
  Pipeline& addFront(std::shared_ptr<H> handler) {
    typedef typename ContextType<H, Pipeline>::type Context;
    return addHelper(std::make_shared<Context>(this, std::move(handler)), true);
  }

  template <class H>
  Pipeline& addFront(H&& handler) {
    return addFront(std::make_shared<H>(std::forward<H>(handler)));
  }

  template <class H>
  Pipeline& addFront(H* handler) {
    return addFront(std::shared_ptr<H>(handler, [](H*){}));
  }

  template <class H>
  H* getHandler(int i) {
    typedef typename ContextType<H, Pipeline>::type Context;
    auto ctx = dynamic_cast<Context*>(ctxs_[i].get());
    CHECK(ctx);
    return ctx->getHandler();
  }

  // TODO Have read/write/etc check that pipeline has been finalized
  void finalize() {
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
      throw std::invalid_argument("no inbound handler in Pipeline");
    }
    if (!back_) {
      throw std::invalid_argument("no outbound handler in Pipeline");
    }

    for (auto it = ctxs_.rbegin(); it != ctxs_.rend(); it++) {
      (*it)->attachPipeline();
    }
  }

  // If one of the handlers owns the pipeline itself, use setOwner to ensure
  // that the pipeline doesn't try to detach the handler during destruction,
  // lest destruction ordering issues occur.
  // See thrift/lib/cpp2/async/Cpp2Channel.cpp for an example
  template <class H>
  bool setOwner(H* handler) {
    typedef typename ContextType<H, Pipeline>::type Context;
    for (auto& ctx : ctxs_) {
      auto ctxImpl = dynamic_cast<Context*>(ctx.get());
      if (ctxImpl && ctxImpl->getHandler() == handler) {
        owner_ = ctx;
        return true;
      }
    }
    return false;
  }

  void attachTransport(
      std::shared_ptr<AsyncTransport> transport) {
    transport_ = std::move(transport);
    for (auto& ctx : ctxs_) {
      ctx->attachTransport();
    }
  }

  void detachTransport() {
    transport_ = nullptr;
    for (auto& ctx : ctxs_) {
      ctx->detachTransport();
    }
  }

 protected:
  explicit Pipeline(bool isStatic) : isStatic_(isStatic) {
    CHECK(isStatic_);
  }

  template <class Context>
  void addContextFront(Context* ctx) {
    addHelper(std::shared_ptr<Context>(ctx, [](Context*){}), true);
  }

  void detachHandlers() {
    for (auto& ctx : ctxs_) {
      if (ctx != owner_) {
        ctx->detachPipeline();
      }
    }
  }

 private:
  template <class Context>
  Pipeline& addHelper(std::shared_ptr<Context>&& ctx, bool front) {
    ctxs_.insert(front ? ctxs_.begin() : ctxs_.end(), ctx);
    if (Context::dir == HandlerDir::BOTH || Context::dir == HandlerDir::IN) {
      inCtxs_.insert(front ? inCtxs_.begin() : inCtxs_.end(), ctx.get());
    }
    if (Context::dir == HandlerDir::BOTH || Context::dir == HandlerDir::OUT) {
      outCtxs_.insert(front ? outCtxs_.begin() : outCtxs_.end(), ctx.get());
    }
    return *this;
  }

  std::shared_ptr<AsyncTransport> transport_;
  WriteFlags writeFlags_{WriteFlags::NONE};
  std::pair<uint64_t, uint64_t> readBufferSettings_{2048, 2048};

  bool isStatic_{false};
  std::shared_ptr<PipelineContext> owner_;
  std::vector<std::shared_ptr<PipelineContext>> ctxs_;
  std::vector<PipelineContext*> inCtxs_;
  std::vector<PipelineContext*> outCtxs_;
  InboundLink<R>* front_{nullptr};
  OutboundLink<W>* back_{nullptr};
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
