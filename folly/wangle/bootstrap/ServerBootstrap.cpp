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
#include <folly/wangle/bootstrap/ServerBootstrap.h>
#include <folly/wangle/concurrent/NamedThreadFactory.h>
#include <folly/wangle/channel/Handler.h>
#include <folly/io/async/EventBaseManager.h>

namespace folly {

void ServerWorkerPool::threadStarted(
  folly::wangle::ThreadPoolExecutor::ThreadHandle* h) {
  auto worker = acceptorFactory_->newAcceptor(exec_->getEventBase(h));
  workers_.insert({h, worker});

  for(auto socket : *sockets_) {
    socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [this, worker, socket](){
        socketFactory_->addAcceptCB(
          socket, worker.get(), worker->getEventBase());
    });
  }
}

void ServerWorkerPool::threadStopped(
  folly::wangle::ThreadPoolExecutor::ThreadHandle* h) {
  auto worker = workers_.find(h);
  CHECK(worker != workers_.end());

  for (auto socket : *sockets_) {
    socket->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() {
        socketFactory_->removeAcceptCB(
          socket, worker->second.get(), nullptr);
    });
  }

  if (!worker->second->getEventBase()->isInEventBaseThread()) {
    worker->second->getEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait(
      [=]() {
        worker->second->dropAllConnections();
      });
  } else {
    worker->second->dropAllConnections();
  }

  workers_.erase(worker);
}

} // namespace
