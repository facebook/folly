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
#include <folly/experimental/wangle/bootstrap/ServerBootstrap.h>
#include <folly/experimental/wangle/concurrent/NamedThreadFactory.h>
#include <folly/io/async/EventBaseManager.h>

namespace folly {

std::thread ServerWorkerFactory::newThread(
    folly::wangle::Func&& func) {
  return internalFactory_->newThread([=](){
    auto id = nextWorkerId_++;
    auto worker = acceptorFactory_->newAcceptor();
    {
      folly::RWSpinLock::WriteHolder guard(workersLock_);
      workers_.insert({id, worker});
    }
    EventBaseManager::get()->setEventBase(worker->getEventBase(), false);
    func();
    EventBaseManager::get()->clearEventBase();

    worker->drainAllConnections();
    {
      folly::RWSpinLock::WriteHolder guard(workersLock_);
      workers_.erase(id);
    }
  });
}

void ServerWorkerFactory::setInternalFactory(
  std::shared_ptr<wangle::NamedThreadFactory> internalFactory) {
  CHECK(workers_.empty());
  internalFactory_ = internalFactory;
}

void ServerWorkerFactory::setNamePrefix(folly::StringPiece prefix) {
  CHECK(workers_.empty());
  internalFactory_->setNamePrefix(prefix);
}

} // namespace
