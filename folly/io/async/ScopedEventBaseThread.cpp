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

#include <folly/io/async/ScopedEventBaseThread.h>

#include <thread>

#include <folly/Memory.h>
#include <folly/io/async/EventBaseManager.h>

using namespace std;

namespace folly {

static void run(EventBaseManager* ebm, EventBase* eb) {
  ebm->setEventBase(eb, false);
  eb->loopForever();
  ebm->clearEventBase();
}

ScopedEventBaseThread::ScopedEventBaseThread()
    : ScopedEventBaseThread(nullptr) {}

ScopedEventBaseThread::ScopedEventBaseThread(EventBaseManager* ebm)
    : ebm_(ebm ? ebm : EventBaseManager::get()) {
  th_ = thread(run, ebm_, &eb_);
  eb_.waitUntilRunning();
}

ScopedEventBaseThread::~ScopedEventBaseThread() {
  eb_.terminateLoopSoon();
  th_.join();
}

}
