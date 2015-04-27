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
    ctxs_.push_back(std::make_shared<ContextImpl<Pipeline, H>>(
        this,
        std::move(handler)));
    return *this;
  }

  template <class H>
  Pipeline& addBack(H* handler) {
    return addBack(std::shared_ptr<H>(handler, [](H*){}));
  }

  template <class H>
  Pipeline& addBack(H&& handler) {
    return addBack(std::make_shared<H>(std::forward<H>(handler)));
  }

  template <class H>
  Pipeline& addFront(std::shared_ptr<H> handler) {
    ctxs_.insert(
        ctxs_.begin(),
        std::make_shared<ContextImpl<Pipeline, H>>(this, std::move(handler)));
    return *this;
  }

  template <class H>
  Pipeline& addFront(H* handler) {
    return addFront(std::shared_ptr<H>(handler, [](H*){}));
  }

  template <class H>
  Pipeline& addFront(H&& handler) {
    return addFront(std::make_shared<H>(std::forward<H>(handler)));
  }

  template <class H>
  H* getHandler(int i) {
    auto ctx = dynamic_cast<ContextImpl<Pipeline, H>*>(ctxs_[i].get());
    CHECK(ctx);
    return ctx->getHandler();
  }

  void finalize() {
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

    front_ = dynamic_cast<InboundHandlerContext<R>*>(ctxs_.front().get());
    if (!front_) {
      throw std::invalid_argument("wrong type for first handler");
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
    for (auto& ctx : ctxs_) {
      auto ctxImpl = dynamic_cast<ContextImpl<Pipeline, H>*>(ctx.get());
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
  void addContextFront(Context* context) {
    ctxs_.insert(
        ctxs_.begin(),
        std::shared_ptr<Context>(context, [](Context*){}));
  }

  void detachHandlers() {
    for (auto& ctx : ctxs_) {
      if (ctx != owner_) {
        ctx->detachPipeline();
      }
    }
  }

 private:
  std::shared_ptr<AsyncTransport> transport_;
  WriteFlags writeFlags_{WriteFlags::NONE};
  std::pair<uint64_t, uint64_t> readBufferSettings_{2048, 2048};

  bool isStatic_{false};
  InboundHandlerContext<R>* front_{nullptr};
  OutboundHandlerContext<W>* back_{nullptr};
  std::vector<std::shared_ptr<PipelineContext>> ctxs_;
  std::shared_ptr<PipelineContext> owner_;
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
