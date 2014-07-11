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

#include <unistd.h>
#include <thread>

#include <gtest/gtest.h>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>

using namespace folly;

class PipeHandler : public EventHandler {
public:
  PipeHandler(EventBase* eventBase, int fd)
    : EventHandler(eventBase, fd) {}

  void handlerReady(uint16_t events) noexcept {
    abort();
  }
};

TEST(EventBase, StopBeforeLoop) {
  EventBase evb;

  // Give the evb something to do.
  int p[2];
  ASSERT_EQ(0, pipe(p));
  PipeHandler handler(&evb, p[0]);
  handler.registerHandler(EventHandler::READ);

  // It's definitely not running yet
  evb.terminateLoopSoon();

  // let it run, it should exit quickly.
  std::thread t([&] { evb.loop(); });
  t.join();

  handler.unregisterHandler();
  close(p[0]);
  close(p[1]);

  SUCCEED();
}
