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

#include <folly/wangle/acceptor/Acceptor.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/wangle/concurrent/IOThreadPoolExecutor.h>
#include <folly/wangle/acceptor/ManagedConnection.h>
#include <folly/wangle/channel/ChannelPipeline.h>

namespace folly {

template <typename Pipeline>
class ServerAcceptor : public Acceptor {
  typedef std::unique_ptr<Pipeline,
                          folly::DelayedDestruction::Destructor> PipelinePtr;

  class ServerConnection : public wangle::ManagedConnection {
   public:
    explicit ServerConnection(PipelinePtr pipeline)
        : pipeline_(std::move(pipeline)) {}

    ~ServerConnection() {
    }

    void timeoutExpired() noexcept {
    }

    void describe(std::ostream& os) const {}
    bool isBusy() const {
      return false;
    }
    void notifyPendingShutdown() {}
    void closeWhenIdle() {}
    void dropConnection() {
      delete this;
    }
    void dumpConnectionState(uint8_t loglevel) {}
   private:
    PipelinePtr pipeline_;
  };

 public:
  explicit ServerAcceptor(
    std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory,
    EventBase* base)
      : Acceptor(ServerSocketConfig())
      , pipelineFactory_(pipelineFactory) {
    Acceptor::init(nullptr, base);
  }

  /* See Acceptor::onNewConnection for details */
  void onNewConnection(
    AsyncSocket::UniquePtr transport, const SocketAddress* address,
    const std::string& nextProtocolName, const TransportInfo& tinfo) {

      std::unique_ptr<Pipeline,
                       folly::DelayedDestruction::Destructor>
      pipeline(pipelineFactory_->newPipeline(
        std::shared_ptr<AsyncSocket>(
          transport.release(),
          folly::DelayedDestruction::Destructor())));
    auto connection = new ServerConnection(std::move(pipeline));
    Acceptor::addConnection(connection);
  }

 private:
  std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory_;
};

template <typename Pipeline>
class ServerAcceptorFactory : public AcceptorFactory {
 public:
  explicit ServerAcceptorFactory(
      std::shared_ptr<PipelineFactory<Pipeline>> factory)
    : factory_(factory) {}

  std::shared_ptr<Acceptor> newAcceptor(folly::EventBase* base) {
    return std::make_shared<ServerAcceptor<Pipeline>>(factory_, base);
  }
 private:
  std::shared_ptr<PipelineFactory<Pipeline>> factory_;
};

class ServerWorkerPool : public folly::wangle::ThreadPoolExecutor::Observer {
 public:
  explicit ServerWorkerPool(
    std::shared_ptr<AcceptorFactory> acceptorFactory,
    folly::wangle::IOThreadPoolExecutor* exec,
    std::vector<std::shared_ptr<folly::AsyncServerSocket>>* sockets)
      : acceptorFactory_(acceptorFactory)
      , exec_(exec)
      , sockets_(sockets) {
    CHECK(exec);
  }

  template <typename F>
  void forEachWorker(F&& f) const;

  void threadStarted(
    folly::wangle::ThreadPoolExecutor::ThreadHandle*);
  void threadStopped(
    folly::wangle::ThreadPoolExecutor::ThreadHandle*);
  void threadPreviouslyStarted(
      folly::wangle::ThreadPoolExecutor::ThreadHandle* thread) {
    threadStarted(thread);
  }
  void threadNotYetStopped(
      folly::wangle::ThreadPoolExecutor::ThreadHandle* thread) {
    threadStopped(thread);
  }

 private:
  std::map<folly::wangle::ThreadPoolExecutor::ThreadHandle*,
           std::shared_ptr<Acceptor>> workers_;
  std::shared_ptr<AcceptorFactory> acceptorFactory_;
  folly::wangle::IOThreadPoolExecutor* exec_{nullptr};
  std::vector<std::shared_ptr<folly::AsyncServerSocket>>* sockets_;
};

template <typename F>
void ServerWorkerPool::forEachWorker(F&& f) const {
  for (const auto& kv : workers_) {
    f(kv.second.get());
  }
}

} // namespace
