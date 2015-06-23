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
#include <folly/futures/Unit.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/wangle/channel/HandlerContext.h>
#include <folly/ExceptionWrapper.h>
#include <folly/Memory.h>

namespace folly { namespace wangle {

class PipelineBase;

class PipelineManager {
 public:
  virtual ~PipelineManager() = default;
  virtual void deletePipeline(PipelineBase* pipeline) = 0;
};

class PipelineBase : public DelayedDestruction {
 public:
  virtual ~PipelineBase() = default;

  void setPipelineManager(PipelineManager* manager) {
    manager_ = manager;
  }

  void deletePipeline() {
    if (manager_) {
      manager_->deletePipeline(this);
    }
  }

  void setTransport(std::shared_ptr<AsyncTransport> transport) {
    transport_ = transport;
  }

  std::shared_ptr<AsyncTransport> getTransport() {
    return transport_;
  }

  void setWriteFlags(WriteFlags flags);
  WriteFlags getWriteFlags();

  void setReadBufferSettings(uint64_t minAvailable, uint64_t allocationSize);
  std::pair<uint64_t, uint64_t> getReadBufferSettings();

  template <class H>
  PipelineBase& addBack(std::shared_ptr<H> handler);

  template <class H>
  PipelineBase& addBack(H&& handler);

  template <class H>
  PipelineBase& addBack(H* handler);

  template <class H>
  PipelineBase& addFront(std::shared_ptr<H> handler);

  template <class H>
  PipelineBase& addFront(H&& handler);

  template <class H>
  PipelineBase& addFront(H* handler);

  template <class H>
  PipelineBase& remove(H* handler);

  template <class H>
  PipelineBase& remove();

  PipelineBase& removeFront();

  PipelineBase& removeBack();

  template <class H>
  H* getHandler(int i);

  // If one of the handlers owns the pipeline itself, use setOwner to ensure
  // that the pipeline doesn't try to detach the handler during destruction,
  // lest destruction ordering issues occur.
  // See thrift/lib/cpp2/async/Cpp2Channel.cpp for an example
  template <class H>
  bool setOwner(H* handler);

  virtual void finalize() = 0;

 protected:
  template <class Context>
  void addContextFront(Context* ctx);

  void detachHandlers();

  std::vector<std::shared_ptr<PipelineContext>> ctxs_;
  std::vector<PipelineContext*> inCtxs_;
  std::vector<PipelineContext*> outCtxs_;

 private:
  PipelineManager* manager_{nullptr};
  std::shared_ptr<AsyncTransport> transport_;

  template <class Context>
  PipelineBase& addHelper(std::shared_ptr<Context>&& ctx, bool front);

  template <class H>
  PipelineBase& removeHelper(H* handler, bool checkEqual);

  typedef std::vector<std::shared_ptr<PipelineContext>>::iterator
    ContextIterator;

  ContextIterator removeAt(const ContextIterator& it);

  WriteFlags writeFlags_{WriteFlags::NONE};
  std::pair<uint64_t, uint64_t> readBufferSettings_{2048, 2048};

  std::shared_ptr<PipelineContext> owner_;
};

/*
 * R is the inbound type, i.e. inbound calls start with pipeline.read(R)
 * W is the outbound type, i.e. outbound calls start with pipeline.write(W)
 *
 * Use Unit for one of the types if your pipeline is unidirectional.
 * If R is Unit, read(), readEOF(), and readException() will be disabled.
 * If W is Unit, write() and close() will be disabled.
 */
template <class R, class W = Unit>
class Pipeline : public PipelineBase {
 public:
  Pipeline();
  ~Pipeline();

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Unit>::value>::type
  read(R msg);

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Unit>::value>::type
  readEOF();

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Unit>::value>::type
  readException(exception_wrapper e);

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Unit>::value>::type
  transportActive();

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Unit>::value>::type
  transportInactive();

  template <class T = W>
  typename std::enable_if<!std::is_same<T, Unit>::value, Future<void>>::type
  write(W msg);

  template <class T = W>
  typename std::enable_if<!std::is_same<T, Unit>::value, Future<void>>::type
  close();

  void finalize() override;

 protected:
  explicit Pipeline(bool isStatic);

 private:
  bool isStatic_{false};

  InboundLink<R>* front_{nullptr};
  OutboundLink<W>* back_{nullptr};
};

}}

namespace folly {

class AsyncSocket;

template <typename Pipeline>
class PipelineFactory {
 public:
  virtual std::unique_ptr<Pipeline, folly::DelayedDestruction::Destructor>
  newPipeline(std::shared_ptr<AsyncSocket>) = 0;

  virtual ~PipelineFactory() = default;
};

}

#include <folly/wangle/channel/Pipeline-inl.h>
