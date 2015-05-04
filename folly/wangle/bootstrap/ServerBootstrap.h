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

#include <folly/wangle/bootstrap/ServerBootstrap-inl.h>
#include <folly/Baton.h>
#include <folly/wangle/channel/Pipeline.h>

namespace folly {

typedef folly::wangle::Pipeline<
  folly::IOBufQueue&, std::unique_ptr<folly::IOBuf>> DefaultPipeline;

/*
 * ServerBootstrap is a parent class intended to set up a
 * high-performance TCP accepting server.  It will manage a pool of
 * accepting threads, any number of accepting sockets, a pool of
 * IO-worker threads, and connection pool for each IO thread for you.
 *
 * The output is given as a Pipeline template: given a
 * PipelineFactory, it will create a new pipeline for each connection,
 * and your server can handle the incoming bytes.
 *
 * BACKWARDS COMPATIBLITY: for servers already taking a pool of
 * Acceptor objects, an AcceptorFactory can be given directly instead
 * of a pipeline factory.
 */
template <typename Pipeline>
class ServerBootstrap {
 public:

  ServerBootstrap(const ServerBootstrap& that) = delete;
  ServerBootstrap(ServerBootstrap&& that) = default;

  ServerBootstrap() {}

  ~ServerBootstrap() {
    stop();
    join();
  }

  typedef wangle::Pipeline<void*> AcceptPipeline;
  /*
   * Pipeline used to add connections to event bases.
   * This is used for UDP or for load balancing
   * TCP connections to IO threads explicitly
   */
  ServerBootstrap* pipeline(
    std::shared_ptr<PipelineFactory<AcceptPipeline>> factory) {
    pipeline_ = factory;
    return this;
  }

  ServerBootstrap* channelFactory(
    std::shared_ptr<ServerSocketFactory> factory) {
    socketFactory_ = factory;
    return this;
  }

  /*
   * BACKWARDS COMPATIBILITY - an acceptor factory can be set.  Your
   * Acceptor is responsible for managing the connection pool.
   *
   * @param childHandler - acceptor factory to call for each IO thread
   */
  ServerBootstrap* childHandler(std::shared_ptr<AcceptorFactory> h) {
    acceptorFactory_ = h;
    return this;
  }

  /*
   * Set a pipeline factory that will be called for each new connection
   *
   * @param factory pipeline factory to use for each new connection
   */
  ServerBootstrap* childPipeline(
      std::shared_ptr<PipelineFactory<Pipeline>> factory) {
    childPipelineFactory_ = factory;
    return this;
  }

  /*
   * Set the IO executor.  If not set, a default one will be created
   * with one thread per core.
   *
   * @param io_group - io executor to use for IO threads.
   */
  ServerBootstrap* group(
      std::shared_ptr<folly::wangle::IOThreadPoolExecutor> io_group) {
    return group(nullptr, io_group);
  }

  /*
   * Set the acceptor executor, and IO executor.
   *
   * If no acceptor executor is set, a single thread will be created for accepts
   * If no IO executor is set, a default of one thread per core will be created
   *
   * @param group - acceptor executor to use for acceptor threads.
   * @param io_group - io executor to use for IO threads.
   */
  ServerBootstrap* group(
      std::shared_ptr<folly::wangle::IOThreadPoolExecutor> accept_group,
      std::shared_ptr<wangle::IOThreadPoolExecutor> io_group) {
    if (!accept_group) {
      accept_group = std::make_shared<folly::wangle::IOThreadPoolExecutor>(
        1, std::make_shared<wangle::NamedThreadFactory>("Acceptor Thread"));
    }
    if (!io_group) {
      io_group = std::make_shared<folly::wangle::IOThreadPoolExecutor>(
        32, std::make_shared<wangle::NamedThreadFactory>("IO Thread"));
    }

    // TODO better config checking
    // CHECK(acceptorFactory_ || childPipelineFactory_);
    CHECK(!(acceptorFactory_ && childPipelineFactory_));

    if (acceptorFactory_) {
      workerFactory_ = std::make_shared<ServerWorkerPool>(
        acceptorFactory_, io_group.get(), sockets_, socketFactory_);
    } else {
      workerFactory_ = std::make_shared<ServerWorkerPool>(
        std::make_shared<ServerAcceptorFactory<Pipeline>>(
          childPipelineFactory_,
          pipeline_),
        io_group.get(), sockets_, socketFactory_);
    }

    io_group->addObserver(workerFactory_);

    acceptor_group_ = accept_group;
    io_group_ = io_group;

    return this;
  }

  /*
   * Bind to an existing socket
   *
   * @param sock Existing socket to use for accepting
   */
  void bind(folly::AsyncServerSocket::UniquePtr s) {
    if (!workerFactory_) {
      group(nullptr);
    }

    // Since only a single socket is given,
    // we can only accept on a single thread
    CHECK(acceptor_group_->numThreads() == 1);

    std::shared_ptr<folly::AsyncServerSocket> socket(
      s.release(), DelayedDestruction::Destructor());

    folly::Baton<> barrier;
    acceptor_group_->add([&](){
      socket->attachEventBase(EventBaseManager::get()->getEventBase());
      socket->listen(socketConfig.acceptBacklog);
      socket->startAccepting();
      barrier.post();
    });
    barrier.wait();

    // Startup all the threads
    workerFactory_->forEachWorker([this, socket](Acceptor* worker){
      socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
        [this, worker, socket](){
          socketFactory_->addAcceptCB(socket, worker, worker->getEventBase());
      });
    });

    sockets_->push_back(socket);
  }

  void bind(folly::SocketAddress& address) {
    bindImpl(-1, address);
  }

  /*
   * Bind to a port and start listening.
   * One of childPipeline or childHandler must be called before bind
   *
   * @param port Port to listen on
   */
  void bind(int port) {
    CHECK(port >= 0);
    folly::SocketAddress address;
    bindImpl(port, address);
  }

  void bindImpl(int port, folly::SocketAddress& address) {
    if (!workerFactory_) {
      group(nullptr);
    }

    bool reusePort = false;
    if (acceptor_group_->numThreads() > 1) {
      reusePort = true;
    }

    std::mutex sock_lock;
    std::vector<std::shared_ptr<folly::AsyncSocketBase>> new_sockets;


    std::exception_ptr exn;

    auto startupFunc = [&](std::shared_ptr<folly::Baton<>> barrier){

      try {
        auto socket = socketFactory_->newSocket(
          port, address, socketConfig.acceptBacklog, reusePort, socketConfig);

        sock_lock.lock();
        new_sockets.push_back(socket);
        sock_lock.unlock();

        if (port <= 0) {
          socket->getAddress(&address);
          port = address.getPort();
        }

        barrier->post();
      } catch (...) {
        exn = std::current_exception();
        barrier->post();

        return;
      }



    };

    auto wait0 = std::make_shared<folly::Baton<>>();
    acceptor_group_->add(std::bind(startupFunc, wait0));
    wait0->wait();

    for (size_t i = 1; i < acceptor_group_->numThreads(); i++) {
      auto barrier = std::make_shared<folly::Baton<>>();
      acceptor_group_->add(std::bind(startupFunc, barrier));
      barrier->wait();
    }

    if (exn) {
      std::rethrow_exception(exn);
    }

    for (auto& socket : new_sockets) {
      // Startup all the threads
      workerFactory_->forEachWorker([this, socket](Acceptor* worker){
        socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
          [this, worker, socket](){
            socketFactory_->addAcceptCB(socket, worker, worker->getEventBase());
        });
      });

      sockets_->push_back(socket);
    }
  }

  /*
   * Stop listening on all sockets.
   */
  void stop() {
    // sockets_ may be null if ServerBootstrap has been std::move'd
    if (sockets_) {
      for (auto socket : *sockets_) {
        socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
          [&]() mutable {
            socketFactory_->stopSocket(socket);
          });
      }
      sockets_->clear();
    }
    if (!stopped_) {
      stopped_ = true;
      // stopBaton_ may be null if ServerBootstrap has been std::move'd
      if (stopBaton_) {
        stopBaton_->post();
      }
    }
  }

  void join() {
    if (acceptor_group_) {
      acceptor_group_->join();
    }
    if (io_group_) {
      io_group_->join();
    }
  }

  void waitForStop() {
    if (!stopped_) {
      CHECK(stopBaton_);
      stopBaton_->wait();
    }
  }

  /*
   * Get the list of listening sockets
   */
  const std::vector<std::shared_ptr<folly::AsyncSocketBase>>&
  getSockets() const {
    return *sockets_;
  }

  std::shared_ptr<wangle::IOThreadPoolExecutor> getIOGroup() const {
    return io_group_;
  }

  template <typename F>
  void forEachWorker(F&& f) const {
    workerFactory_->forEachWorker(f);
  }

  ServerSocketConfig socketConfig;

 private:
  std::shared_ptr<wangle::IOThreadPoolExecutor> acceptor_group_;
  std::shared_ptr<wangle::IOThreadPoolExecutor> io_group_;

  std::shared_ptr<ServerWorkerPool> workerFactory_;
  std::shared_ptr<std::vector<std::shared_ptr<folly::AsyncSocketBase>>> sockets_{
    std::make_shared<std::vector<std::shared_ptr<folly::AsyncSocketBase>>>()};

  std::shared_ptr<AcceptorFactory> acceptorFactory_;
  std::shared_ptr<PipelineFactory<Pipeline>> childPipelineFactory_;
  std::shared_ptr<PipelineFactory<AcceptPipeline>> pipeline_{
    std::make_shared<DefaultAcceptPipelineFactory>()};
  std::shared_ptr<ServerSocketFactory> socketFactory_{
    std::make_shared<AsyncServerSocketFactory>()};

  std::unique_ptr<folly::Baton<>> stopBaton_{
    folly::make_unique<folly::Baton<>>()};
  bool stopped_{false};
};

} // namespace
