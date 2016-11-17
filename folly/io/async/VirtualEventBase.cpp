/*
 * Copyright 2016 Facebook, Inc.
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
#include <folly/io/async/VirtualEventBase.h>

namespace folly {

VirtualEventBase::VirtualEventBase(EventBase& evb) : evb_(evb) {
  evbLoopKeepAlive_ = evb_.loopKeepAliveAtomic();
  loopKeepAlive_ = loopKeepAliveAtomic();
}

VirtualEventBase::~VirtualEventBase() {
  CHECK(!evb_.inRunningEventBaseThread());

  CHECK(evb_.runInEventBaseThread([&] { loopKeepAlive_.reset(); }));
  loopKeepAliveBaton_.wait();

  CHECK(evb_.runInEventBaseThreadAndWait([&] {
    clearCobTimeouts();

    onDestructionCallbacks_.withWLock([&](LoopCallbackList& callbacks) {
      while (!callbacks.empty()) {
        auto& callback = callbacks.front();
        callbacks.pop_front();
        callback.runLoopCallback();
      }
    });

    evbLoopKeepAlive_.reset();
  }));
}

void VirtualEventBase::runOnDestruction(EventBase::LoopCallback* callback) {
  onDestructionCallbacks_.withWLock([&](LoopCallbackList& callbacks) {
    callback->cancelLoopCallback();
    callbacks.push_back(*callback);
  });
}
}
