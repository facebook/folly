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

#include <folly/io/async/AsyncTransport.h>
#include <folly/futures/Future.h>
#include <folly/ExceptionWrapper.h>

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

  /* TODO
  template <class H>
  virtual void addHandlerBefore(H&&) {}
  template <class H>
  virtual void addHandlerAfter(H&&) {}
  template <class H>
  virtual void replaceHandler(H&&) {}
  virtual void removeHandler() {}
  */
};

class PipelineContext {
 public:
  virtual ~PipelineContext() {}

  virtual void attachTransport() = 0;
  virtual void detachTransport() = 0;

  void link(PipelineContext* other) {
    setNextIn(other);
    other->setNextOut(this);
  }

 protected:
  virtual void setNextIn(PipelineContext* ctx) = 0;
  virtual void setNextOut(PipelineContext* ctx) = 0;
};

template <class In>
class InboundChannelHandlerContext {
 public:
  virtual ~InboundChannelHandlerContext() {}
  virtual void read(In msg) = 0;
  virtual void readEOF() = 0;
  virtual void readException(exception_wrapper e) = 0;
};

template <class Out>
class OutboundChannelHandlerContext {
 public:
  virtual ~OutboundChannelHandlerContext() {}
  virtual Future<void> write(Out msg) = 0;
  virtual Future<void> close() = 0;
};

template <class P, class H>
class ContextImpl : public ChannelHandlerContext<typename H::rout,
                                                 typename H::wout>,
                    public InboundChannelHandlerContext<typename H::rin>,
                    public OutboundChannelHandlerContext<typename H::win>,
                    public PipelineContext {
 public:
  typedef typename H::rin Rin;
  typedef typename H::rout Rout;
  typedef typename H::win Win;
  typedef typename H::wout Wout;

  template <class HandlerArg>
  explicit ContextImpl(P* pipeline, HandlerArg&& handlerArg)
    : pipeline_(pipeline),
      handler_(std::forward<HandlerArg>(handlerArg)) {
    handler_.attachPipeline(this);
  }

  ~ContextImpl() {
    handler_.detachPipeline(this);
  }

  H* getHandler() {
    return &handler_;
  }

  // PipelineContext overrides
  void setNextIn(PipelineContext* ctx) override {
    auto nextIn = dynamic_cast<InboundChannelHandlerContext<Rout>*>(ctx);
    if (nextIn) {
      nextIn_ = nextIn;
    } else {
      throw std::invalid_argument("wrong type in setNextIn");
    }
  }

  void setNextOut(PipelineContext* ctx) override {
    auto nextOut = dynamic_cast<OutboundChannelHandlerContext<Wout>*>(ctx);
    if (nextOut) {
      nextOut_ = nextOut;
    } else {
      throw std::invalid_argument("wrong type in setNextOut");
    }
  }

  void attachTransport() override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    handler_.attachTransport(this);
  }

  void detachTransport() override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    handler_.detachTransport(this);
  }

  // ChannelHandlerContext overrides
  void fireRead(Rout msg) override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    if (nextIn_) {
      nextIn_->read(std::forward<Rout>(msg));
    } else {
      LOG(WARNING) << "read reached end of pipeline";
    }
  }

  void fireReadEOF() override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    if (nextIn_) {
      nextIn_->readEOF();
    } else {
      LOG(WARNING) << "readEOF reached end of pipeline";
    }
  }

  void fireReadException(exception_wrapper e) override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    if (nextIn_) {
      nextIn_->readException(std::move(e));
    } else {
      LOG(WARNING) << "readException reached end of pipeline";
    }
  }

  Future<void> fireWrite(Wout msg) override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    if (nextOut_) {
      return nextOut_->write(std::forward<Wout>(msg));
    } else {
      LOG(WARNING) << "write reached end of pipeline";
      return makeFuture();
    }
  }

  Future<void> fireClose() override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    if (nextOut_) {
      return nextOut_->close();
    } else {
      LOG(WARNING) << "close reached end of pipeline";
      return makeFuture();
    }
  }

  std::shared_ptr<AsyncTransport> getTransport() override {
    return pipeline_->getTransport();
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

  // InboundChannelHandlerContext overrides
  void read(Rin msg) override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    handler_.read(this, std::forward<Rin>(msg));
  }

  void readEOF() override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    handler_.readEOF(this);
  }

  void readException(exception_wrapper e) override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    handler_.readException(this, std::move(e));
  }

  // OutboundChannelHandlerContext overrides
  Future<void> write(Win msg) override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    return handler_.write(this, std::forward<Win>(msg));
  }

  Future<void> close() override {
    typename P::DestructorGuard dg(static_cast<DelayedDestruction*>(pipeline_));
    return handler_.close(this);
  }

 private:
  P* pipeline_;
  H handler_;
  InboundChannelHandlerContext<Rout>* nextIn_{nullptr};
  OutboundChannelHandlerContext<Wout>* nextOut_{nullptr};
};

}}
