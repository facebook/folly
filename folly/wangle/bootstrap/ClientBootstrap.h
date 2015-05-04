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

#include <folly/wangle/channel/Pipeline.h>
#include <folly/wangle/concurrent/IOThreadPoolExecutor.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBaseManager.h>

namespace folly {

/*
 * A thin wrapper around Pipeline and AsyncSocket to match
 * ServerBootstrap.  On connect() a new pipeline is created.
 */
template <typename Pipeline>
class ClientBootstrap {

  class ConnectCallback : public AsyncSocket::ConnectCallback {
   public:
    ConnectCallback(Promise<Pipeline*> promise, ClientBootstrap* bootstrap)
        : promise_(std::move(promise))
        , bootstrap_(bootstrap) {}

    void connectSuccess() noexcept override {
      promise_.setValue(bootstrap_->getPipeline());
      delete this;
    }

    void connectErr(const AsyncSocketException& ex) noexcept override {
      promise_.setException(
        folly::make_exception_wrapper<AsyncSocketException>(ex));
      delete this;
    }
   private:
    Promise<Pipeline*> promise_;
    ClientBootstrap* bootstrap_;
  };

 public:
  ClientBootstrap() {
  }

  ClientBootstrap* group(
      std::shared_ptr<folly::wangle::IOThreadPoolExecutor> group) {
    group_ = group;
    return this;
  }
  ClientBootstrap* bind(int port) {
    port_ = port;
    return this;
  }
  Future<Pipeline*> connect(SocketAddress address) {
    DCHECK(pipelineFactory_);
    auto base = EventBaseManager::get()->getEventBase();
    if (group_) {
      base = group_->getEventBase();
    }
    Future<Pipeline*> retval((Pipeline*)nullptr);
    base->runImmediatelyOrRunInEventBaseThreadAndWait([&](){
      auto socket = AsyncSocket::newSocket(base);
      Promise<Pipeline*> promise;
      retval = promise.getFuture();
      socket->connect(
        new ConnectCallback(std::move(promise), this), address);
      pipeline_ = pipelineFactory_->newPipeline(socket);
      if (pipeline_) {
        pipeline_->attachTransport(socket);
      }
    });
    return retval;
  }

  ClientBootstrap* pipelineFactory(
      std::shared_ptr<PipelineFactory<Pipeline>> factory) {
    pipelineFactory_ = factory;
    return this;
  }

  Pipeline* getPipeline() {
    return pipeline_.get();
  }

  virtual ~ClientBootstrap() {}

 protected:
  std::unique_ptr<Pipeline,
                  folly::DelayedDestruction::Destructor> pipeline_;

  int port_;

  std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory_;
  std::shared_ptr<folly::wangle::IOThreadPoolExecutor> group_;
};

} // namespace
