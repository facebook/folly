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

#include <folly/experimental/wangle/acceptor/Acceptor.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/experimental/wangle/concurrent/IOThreadPoolExecutor.h>
#include <folly/experimental/wangle/ManagedConnection.h>
#include <folly/experimental/wangle/channel/ChannelPipeline.h>

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
    void dropConnection() {}
    void dumpConnectionState(uint8_t loglevel) {}
   private:
    PipelinePtr pipeline_;
  };

 public:
  explicit ServerAcceptor(
    std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory)
      : Acceptor(ServerSocketConfig())
      , pipelineFactory_(pipelineFactory) {
    Acceptor::init(nullptr, &base_);
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

  ~ServerAcceptor() {
    Acceptor::dropAllConnections();
  }

 private:
  EventBase base_;

  std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory_;
};

template <typename Pipeline>
class ServerAcceptorFactory : public AcceptorFactory {
 public:
  explicit ServerAcceptorFactory(
      std::shared_ptr<PipelineFactory<Pipeline>> factory)
    : factory_(factory) {}

  std::shared_ptr<Acceptor> newAcceptor() {
    return std::make_shared<ServerAcceptor<Pipeline>>(factory_);
  }
 private:
  std::shared_ptr<PipelineFactory<Pipeline>> factory_;
};

class ServerWorkerFactory : public folly::wangle::ThreadFactory {
 public:
  explicit ServerWorkerFactory(std::shared_ptr<AcceptorFactory> acceptorFactory)
      : internalFactory_(
        std::make_shared<folly::wangle::NamedThreadFactory>("BootstrapWorker"))
      , acceptorFactory_(acceptorFactory)
    {}
  virtual std::thread newThread(folly::Func&& func) override;

  void setInternalFactory(
    std::shared_ptr<folly::wangle::NamedThreadFactory> internalFactory);
  void setNamePrefix(folly::StringPiece prefix);

  template <typename F>
  void forEachWorker(F&& f);

 private:
  std::shared_ptr<folly::wangle::NamedThreadFactory> internalFactory_;
  folly::RWSpinLock workersLock_;
  std::map<int32_t, std::shared_ptr<Acceptor>> workers_;
  int32_t nextWorkerId_{0};

  std::shared_ptr<AcceptorFactory> acceptorFactory_;
};

template <typename F>
void ServerWorkerFactory::forEachWorker(F&& f) {
  folly::RWSpinLock::ReadHolder guard(workersLock_);
  for (const auto& kv : workers_) {
    f(kv.second.get());
  }
}

} // namespace
