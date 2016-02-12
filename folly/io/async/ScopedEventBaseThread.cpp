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

using namespace std;

namespace folly {

static void run(EventBaseManager* ebm, EventBase* eb) {
  if (ebm) {
    ebm->setEventBase(eb, false);
  }
  CHECK_NOTNULL(eb)->loopForever();
  if (ebm) {
    ebm->clearEventBase();
  }
}

ScopedEventBaseThread::ScopedEventBaseThread(
    bool autostart,
    EventBaseManager* ebm) :
  ebm_(ebm) {
  if (autostart) {
    start();
  }
}

ScopedEventBaseThread::ScopedEventBaseThread(
    EventBaseManager* ebm) :
  ScopedEventBaseThread(true, ebm) {}

ScopedEventBaseThread::~ScopedEventBaseThread() {
  stop();
}

ScopedEventBaseThread::ScopedEventBaseThread(
    ScopedEventBaseThread&& /* other */) noexcept = default;

ScopedEventBaseThread& ScopedEventBaseThread::operator=(
    ScopedEventBaseThread&& /* other */) noexcept = default;

void ScopedEventBaseThread::start() {
  if (running()) {
    return;
  }
  eventBase_ = make_unique<EventBase>();
  thread_ = make_unique<thread>(run, ebm_, eventBase_.get());
  eventBase_->waitUntilRunning();
}

void ScopedEventBaseThread::stop() {
  if (!running()) {
    return;
  }
  eventBase_->terminateLoopSoon();
  thread_->join();
  eventBase_ = nullptr;
  thread_ = nullptr;
}

bool ScopedEventBaseThread::running() {
  CHECK(bool(eventBase_) == bool(thread_));
  return eventBase_ && thread_;
}

}
