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

namespace folly { namespace wangle {

class PipelineContext {
 public:
  virtual ~PipelineContext() {}

  virtual void attachPipeline() = 0;
  virtual void detachPipeline() = 0;

  virtual void attachTransport() = 0;
  virtual void detachTransport() = 0;

  template <class H, class HandlerContext>
  void attachContext(H* handler, HandlerContext* ctx) {
    if (++handler->attachCount_ == 1) {
      handler->ctx_ = ctx;
    } else {
      handler->ctx_ = nullptr;
    }
  }

  virtual void setNextIn(PipelineContext* ctx) = 0;
  virtual void setNextOut(PipelineContext* ctx) = 0;
};

template <class In>
class InboundLink {
 public:
  virtual ~InboundLink() {}
  virtual void read(In msg) = 0;
  virtual void readEOF() = 0;
  virtual void readException(exception_wrapper e) = 0;
};

template <class Out>
class OutboundLink {
 public:
  virtual ~OutboundLink() {}
  virtual Future<void> write(Out msg) = 0;
  virtual Future<void> close() = 0;
};

template <class P, class H, class Context>
class ContextImplBase : public PipelineContext {
 public:
  ~ContextImplBase() {}

  H* getHandler() {
    return handler_.get();
  }

  void initialize(P* pipeline, std::shared_ptr<H> handler) {
    pipeline_ = pipeline;
    handler_ = std::move(handler);
  }

  // PipelineContext overrides
  void attachPipeline() override {
    if (!attached_) {
      this->attachContext(handler_.get(), impl_);
      handler_->attachPipeline(impl_);
      attached_ = true;
    }
  }

  void detachPipeline() override {
    handler_->detachPipeline(impl_);
    attached_ = false;
  }

  void attachTransport() override {
    DestructorGuard dg(pipeline_);
    handler_->attachTransport(impl_);
  }

  void detachTransport() override {
    DestructorGuard dg(pipeline_);
    handler_->detachTransport(impl_);
  }

  void setNextIn(PipelineContext* ctx) override {
    auto nextIn = dynamic_cast<InboundLink<typename H::rout>*>(ctx);
    if (nextIn) {
      nextIn_ = nextIn;
    } else {
      throw std::invalid_argument("inbound type mismatch");
    }
  }

  void setNextOut(PipelineContext* ctx) override {
    auto nextOut = dynamic_cast<OutboundLink<typename H::wout>*>(ctx);
    if (nextOut) {
      nextOut_ = nextOut;
    } else {
      throw std::invalid_argument("outbound type mismatch");
    }
  }

 protected:
  Context* impl_;
  P* pipeline_;
  std::shared_ptr<H> handler_;
  InboundLink<typename H::rout>* nextIn_{nullptr};
  OutboundLink<typename H::wout>* nextOut_{nullptr};

 private:
  bool attached_{false};
  using DestructorGuard = typename P::DestructorGuard;
};

template <class P, class H>
class ContextImpl
  : public HandlerContext<typename H::rout,
                          typename H::wout>,
    public InboundLink<typename H::rin>,
    public OutboundLink<typename H::win>,
    public ContextImplBase<P, H, HandlerContext<typename H::rout,
                                                typename H::wout>> {
 public:
  typedef typename H::rin Rin;
  typedef typename H::rout Rout;
  typedef typename H::win Win;
  typedef typename H::wout Wout;
  static const HandlerDir dir = HandlerDir::BOTH;

  explicit ContextImpl(P* pipeline, std::shared_ptr<H> handler) {
    this->impl_ = this;
    this->initialize(pipeline, std::move(handler));
  }

  // For StaticPipeline
  ContextImpl() {
    this->impl_ = this;
  }

  ~ContextImpl() {}

  // HandlerContext overrides
  void fireRead(Rout msg) override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextIn_) {
      this->nextIn_->read(std::forward<Rout>(msg));
    } else {
      LOG(WARNING) << "read reached end of pipeline";
    }
  }

  void fireReadEOF() override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextIn_) {
      this->nextIn_->readEOF();
    } else {
      LOG(WARNING) << "readEOF reached end of pipeline";
    }
  }

  void fireReadException(exception_wrapper e) override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextIn_) {
      this->nextIn_->readException(std::move(e));
    } else {
      LOG(WARNING) << "readException reached end of pipeline";
    }
  }

  Future<void> fireWrite(Wout msg) override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextOut_) {
      return this->nextOut_->write(std::forward<Wout>(msg));
    } else {
      LOG(WARNING) << "write reached end of pipeline";
      return makeFuture();
    }
  }

  Future<void> fireClose() override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextOut_) {
      return this->nextOut_->close();
    } else {
      LOG(WARNING) << "close reached end of pipeline";
      return makeFuture();
    }
  }

  PipelineBase* getPipeline() override {
    return this->pipeline_;
  }

  std::shared_ptr<AsyncTransport> getTransport() override {
    return this->pipeline_->getTransport();
  }

  void setWriteFlags(WriteFlags flags) override {
    this->pipeline_->setWriteFlags(flags);
  }

  WriteFlags getWriteFlags() override {
    return this->pipeline_->getWriteFlags();
  }

  void setReadBufferSettings(
      uint64_t minAvailable,
      uint64_t allocationSize) override {
    this->pipeline_->setReadBufferSettings(minAvailable, allocationSize);
  }

  std::pair<uint64_t, uint64_t> getReadBufferSettings() override {
    return this->pipeline_->getReadBufferSettings();
  }

  // InboundLink overrides
  void read(Rin msg) override {
    DestructorGuard dg(this->pipeline_);
    this->handler_->read(this, std::forward<Rin>(msg));
  }

  void readEOF() override {
    DestructorGuard dg(this->pipeline_);
    this->handler_->readEOF(this);
  }

  void readException(exception_wrapper e) override {
    DestructorGuard dg(this->pipeline_);
    this->handler_->readException(this, std::move(e));
  }

  // OutboundLink overrides
  Future<void> write(Win msg) override {
    DestructorGuard dg(this->pipeline_);
    return this->handler_->write(this, std::forward<Win>(msg));
  }

  Future<void> close() override {
    DestructorGuard dg(this->pipeline_);
    return this->handler_->close(this);
  }

 private:
  using DestructorGuard = typename P::DestructorGuard;
};

template <class P, class H>
class InboundContextImpl
  : public InboundHandlerContext<typename H::rout>,
    public InboundLink<typename H::rin>,
    public ContextImplBase<P, H, InboundHandlerContext<typename H::rout>> {
 public:
  typedef typename H::rin Rin;
  typedef typename H::rout Rout;
  typedef typename H::win Win;
  typedef typename H::wout Wout;
  static const HandlerDir dir = HandlerDir::IN;

  explicit InboundContextImpl(P* pipeline, std::shared_ptr<H> handler) {
    this->impl_ = this;
    this->initialize(pipeline, std::move(handler));
  }

  // For StaticPipeline
  InboundContextImpl() {
    this->impl_ = this;
  }

  ~InboundContextImpl() {}

  // InboundHandlerContext overrides
  void fireRead(Rout msg) override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextIn_) {
      this->nextIn_->read(std::forward<Rout>(msg));
    } else {
      LOG(WARNING) << "read reached end of pipeline";
    }
  }

  void fireReadEOF() override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextIn_) {
      this->nextIn_->readEOF();
    } else {
      LOG(WARNING) << "readEOF reached end of pipeline";
    }
  }

  void fireReadException(exception_wrapper e) override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextIn_) {
      this->nextIn_->readException(std::move(e));
    } else {
      LOG(WARNING) << "readException reached end of pipeline";
    }
  }

  PipelineBase* getPipeline() override {
    return this->pipeline_;
  }

  std::shared_ptr<AsyncTransport> getTransport() override {
    return this->pipeline_->getTransport();
  }

  // InboundLink overrides
  void read(Rin msg) override {
    DestructorGuard dg(this->pipeline_);
    this->handler_->read(this, std::forward<Rin>(msg));
  }

  void readEOF() override {
    DestructorGuard dg(this->pipeline_);
    this->handler_->readEOF(this);
  }

  void readException(exception_wrapper e) override {
    DestructorGuard dg(this->pipeline_);
    this->handler_->readException(this, std::move(e));
  }

 private:
  using DestructorGuard = typename P::DestructorGuard;
};

template <class P, class H>
class OutboundContextImpl
  : public OutboundHandlerContext<typename H::wout>,
    public OutboundLink<typename H::win>,
    public ContextImplBase<P, H, OutboundHandlerContext<typename H::wout>> {
 public:
  typedef typename H::rin Rin;
  typedef typename H::rout Rout;
  typedef typename H::win Win;
  typedef typename H::wout Wout;
  static const HandlerDir dir = HandlerDir::OUT;

  explicit OutboundContextImpl(P* pipeline, std::shared_ptr<H> handler) {
    this->impl_ = this;
    this->initialize(pipeline, std::move(handler));
  }

  // For StaticPipeline
  OutboundContextImpl() {
    this->impl_ = this;
  }

  ~OutboundContextImpl() {}

  // OutboundHandlerContext overrides
  Future<void> fireWrite(Wout msg) override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextOut_) {
      return this->nextOut_->write(std::forward<Wout>(msg));
    } else {
      LOG(WARNING) << "write reached end of pipeline";
      return makeFuture();
    }
  }

  Future<void> fireClose() override {
    DestructorGuard dg(this->pipeline_);
    if (this->nextOut_) {
      return this->nextOut_->close();
    } else {
      LOG(WARNING) << "close reached end of pipeline";
      return makeFuture();
    }
  }

  PipelineBase* getPipeline() override {
    return this->pipeline_;
  }

  std::shared_ptr<AsyncTransport> getTransport() override {
    return this->pipeline_->getTransport();
  }

  // OutboundLink overrides
  Future<void> write(Win msg) override {
    DestructorGuard dg(this->pipeline_);
    return this->handler_->write(this, std::forward<Win>(msg));
  }

  Future<void> close() override {
    DestructorGuard dg(this->pipeline_);
    return this->handler_->close(this);
  }

 private:
  using DestructorGuard = typename P::DestructorGuard;
};

template <class Handler, class Pipeline>
struct ContextType {
  typedef typename std::conditional<
    Handler::dir == HandlerDir::BOTH,
    ContextImpl<Pipeline, Handler>,
    typename std::conditional<
      Handler::dir == HandlerDir::IN,
      InboundContextImpl<Pipeline, Handler>,
      OutboundContextImpl<Pipeline, Handler>
    >::type>::type
  type;
};

}} // folly::wangle
