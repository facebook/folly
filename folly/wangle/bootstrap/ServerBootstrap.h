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
#include <boost/thread.hpp>

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
    // TODO take existing socket

    if (!workerFactory_) {
      group(nullptr);
    }

    bool reusePort = false;
    if (acceptor_group_->numThreads() > 1) {
      reusePort = true;
    }

    std::mutex sock_lock;
    std::vector<std::shared_ptr<folly::AsyncServerSocket>> new_sockets;

    auto startupFunc = [&](std::shared_ptr<boost::barrier> barrier){
        auto socket = folly::AsyncServerSocket::newSocket();
        sock_lock.lock();
        new_sockets.push_back(socket);
        sock_lock.unlock();
        socket->setReusePortEnabled(reusePort);
        socket->attachEventBase(EventBaseManager::get()->getEventBase());
        socket->bind(port);
        // TODO Take ServerSocketConfig
        socket->listen(1024);
        socket->startAccepting();

        if (port == 0) {
          SocketAddress address;
          socket->getAddress(&address);
          port = address.getPort();
        }

        barrier->wait();
    };

    auto bind0 = std::make_shared<boost::barrier>(2);
    acceptor_group_->add(std::bind(startupFunc, bind0));
    bind0->wait();

    auto barrier = std::make_shared<boost::barrier>(acceptor_group_->numThreads());
    for (size_t i = 1; i < acceptor_group_->numThreads(); i++) {
      acceptor_group_->add(std::bind(startupFunc, barrier));
    }
    barrier->wait();

    // Startup all the threads
    for(auto socket : new_sockets) {
      workerFactory_->forEachWorker([this, socket](Acceptor* worker){
        socket->getEventBase()->runInEventBaseThread([this, worker, socket](){
          socket->addAcceptCallback(worker, worker->getEventBase());
        });
      });
    }

    for (auto& socket : new_sockets) {
      sockets_.push_back(socket);
    }
  }

  /*
   * Stop listening on all sockets.
   */
  void stop() {
    auto barrier = std::make_shared<boost::barrier>(sockets_.size() + 1);
    for (auto socket : sockets_) {
      socket->getEventBase()->runInEventBaseThread([barrier, socket]() {
        socket->stopAccepting();
        socket->detachEventBase();
        barrier->wait();
      });
    }
    barrier->wait();
    sockets_.clear();

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
