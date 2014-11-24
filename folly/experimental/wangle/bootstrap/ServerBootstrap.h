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

#include <folly/experimental/wangle/bootstrap/ServerBootstrap-inl.h>

namespace folly {

/*
 * ServerBootstrap is a parent class intended to set up a
 * high-performance TCP accepting server.  It will manage a pool of
 * accepting threads, any number of accepting sockets, a pool of
 * IO-worker threads, and connection pool for each IO thread for you.
 *
 * The output is given as a ChannelPipeline template: given a
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
  /* TODO(davejwatson)
   *
   * If there is any work to be done BEFORE handing the work to IO
   * threads, this handler is where the pipeline to do it would be
   * set.
   *
   * This could be used for things like logging, load balancing, or
   * advanced load balancing on IO threads.  Netty also provides this.
   */
  ServerBootstrap* handler() {
    return this;
  }

  /*
   * BACKWARDS COMPATIBILITY - an acceptor factory can be set.  Your
   * Acceptor is responsible for managing the connection pool.
   *
   * @param childHandler - acceptor factory to call for each IO thread
   */
  ServerBootstrap* childHandler(std::shared_ptr<AcceptorFactory> childHandler) {
    acceptorFactory_ = childHandler;
    return this;
  }

  /*
   * Set a pipeline factory that will be called for each new connection
   *
   * @param factory pipeline factory to use for each new connection
   */
  ServerBootstrap* childPipeline(
      std::shared_ptr<PipelineFactory<Pipeline>> factory) {
    pipelineFactory_ = factory;
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
    auto factoryBase = io_group->getThreadFactory();
    CHECK(factoryBase);
    auto factory = std::dynamic_pointer_cast<folly::wangle::NamedThreadFactory>(
      factoryBase);
    CHECK(factory); // Must be named thread factory

    CHECK(acceptorFactory_ || pipelineFactory_);

    if (acceptorFactory_) {
      workerFactory_ = std::make_shared<ServerWorkerFactory>(
        acceptorFactory_);
    } else {
      workerFactory_ = std::make_shared<ServerWorkerFactory>(
        std::make_shared<ServerAcceptorFactory<Pipeline>>(pipelineFactory_));
    }
    workerFactory_->setInternalFactory(factory);

    acceptor_group_ = accept_group;
    io_group_ = io_group;

    auto numThreads = io_group_->numThreads();
    io_group_->setNumThreads(0);
    io_group_->setThreadFactory(workerFactory_);
    io_group_->setNumThreads(numThreads);

    return this;
  }

  /*
   * Bind to a port and start listening.
   * One of childPipeline or childHandler must be called before bind
   *
   * @param port Port to listen on
   */
  void bind(int port) {
    // TODO bind to v4 and v6
    // TODO take existing socket
    // TODO use the acceptor thread
    auto socket = folly::AsyncServerSocket::newSocket(
      EventBaseManager::get()->getEventBase());
    sockets_.push_back(socket);
    socket->bind(port);

    // TODO Take ServerSocketConfig
    socket->listen(1024);

    if (!workerFactory_) {
      group(nullptr);
    }

    // Startup all the threads
    workerFactory_->forEachWorker([this, socket](Acceptor* worker){
        socket->getEventBase()->runInEventBaseThread([this, worker, socket](){
          socket->addAcceptCallback(worker, worker->getEventBase());
        });
    });
    socket->startAccepting();
  }

  /*
   * Stop listening on all sockets.
   */
  void stop() {
    for (auto& socket : sockets_) {
      socket->stopAccepting();
    }
    acceptor_group_->join();
    io_group_->join();
  }

  /*
   * Get the list of listening sockets
   */
  std::vector<std::shared_ptr<folly::AsyncServerSocket>>&
  getSockets() {
    return sockets_;
  }

 private:
  std::shared_ptr<wangle::IOThreadPoolExecutor> acceptor_group_;
  std::shared_ptr<wangle::IOThreadPoolExecutor> io_group_;

  std::shared_ptr<ServerWorkerFactory> workerFactory_;
  std::vector<std::shared_ptr<folly::AsyncServerSocket>> sockets_;

  std::shared_ptr<AcceptorFactory> acceptorFactory_;
  std::shared_ptr<PipelineFactory<Pipeline>> pipelineFactory_;
};

} // namespace
