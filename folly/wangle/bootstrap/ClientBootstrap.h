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

namespace folly {

/*
 * A thin wrapper around Pipeline and AsyncSocket to match
 * ServerBootstrap.  On connect() a new pipeline is created.
 */
template <typename Pipeline>
class ClientBootstrap {
 public:
  ClientBootstrap() {
  }
  ClientBootstrap* bind(int port) {
    port_ = port;
    return this;
  }
  ClientBootstrap* connect(SocketAddress address) {
    DCHECK(pipelineFactory_);
    pipeline_.reset(
      pipelineFactory_->newPipeline(
        AsyncSocket::newSocket(EventBaseManager::get()->getEventBase(), address)
      ));
    return this;
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
};

} // namespace
