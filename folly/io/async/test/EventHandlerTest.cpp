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

#include <folly/io/async/EventHandler.h>

#include <sys/eventfd.h>
#include <thread>
#include <folly/MPMCQueue.h>
#include <folly/ScopeGuard.h>
#include <folly/io/async/EventBase.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace std;
using namespace folly;
using namespace testing;

void runInThreadsAndWait(
    size_t nthreads, function<void(size_t)> cb) {
  vector<thread> threads(nthreads);
  for (size_t i = 0; i < nthreads; ++i) {
    threads[i] = thread(cb, i);
  }
  for (size_t i = 0; i < nthreads; ++i) {
    threads[i].join();
  }
}

void runInThreadsAndWait(vector<function<void()>> cbs) {
  runInThreadsAndWait(cbs.size(), [&](size_t k) { cbs[k](); });
}

class EventHandlerMock : public EventHandler {
public:
  EventHandlerMock(EventBase* eb, int fd) : EventHandler(eb, fd) {}
  // gmock can't mock noexcept methods, so we need an intermediary
  MOCK_METHOD1(_handlerReady, void(uint16_t));
  void handlerReady(uint16_t events) noexcept override {
    _handlerReady(events);
  }
};

class EventHandlerTest : public Test {
public:
  int efd = 0;

  void SetUp() override {
    efd = eventfd(0, EFD_SEMAPHORE);
    ASSERT_THAT(efd, Gt(0));
  }

  void TearDown() override {
    if (efd > 0) {
      close(efd);
    }
    efd = 0;
  }

  void efd_write(uint64_t val) {
    write(efd, &val, sizeof(val));
  }

  uint64_t efd_read() {
    uint64_t val = 0;
    read(efd, &val, sizeof(val));
    return val;
  }
};

TEST_F(EventHandlerTest, simple) {
  const size_t writes = 4;
  size_t readsRemaining = writes;

  EventBase eb;
  EventHandlerMock eh(&eb, efd);
  eh.registerHandler(EventHandler::READ | EventHandler::PERSIST);
  EXPECT_CALL(eh, _handlerReady(_))
      .Times(writes)
      .WillRepeatedly(Invoke([&](uint16_t /* events */) {
        efd_read();
        if (--readsRemaining == 0) {
          eh.unregisterHandler();
        }
      }));
  efd_write(writes);
  eb.loop();

  EXPECT_EQ(0, readsRemaining);
}

TEST_F(EventHandlerTest, many_concurrent_producers) {
  const size_t writes = 200;
  const size_t nproducers = 20;
  size_t readsRemaining = writes;

  runInThreadsAndWait({
      [&] {
        EventBase eb;
        EventHandlerMock eh(&eb, efd);
        eh.registerHandler(EventHandler::READ | EventHandler::PERSIST);
        EXPECT_CALL(eh, _handlerReady(_))
            .Times(writes)
            .WillRepeatedly(Invoke([&](uint16_t /* events */) {
              efd_read();
              if (--readsRemaining == 0) {
                eh.unregisterHandler();
              }
            }));
        eb.loop();
      },
      [&] {
        runInThreadsAndWait(nproducers,
                            [&](size_t /* k */) {
                              for (size_t i = 0; i < writes / nproducers; ++i) {
                                this_thread::sleep_for(chrono::milliseconds(1));
                                efd_write(1);
                              }
                            });
      },
  });

  EXPECT_EQ(0, readsRemaining);
}

TEST_F(EventHandlerTest, many_concurrent_consumers) {
  const size_t writes = 200;
  const size_t nproducers = 8;
  const size_t nconsumers = 20;
  atomic<size_t> writesRemaining(writes);
  atomic<size_t> readsRemaining(writes);

  MPMCQueue<nullptr_t> queue(writes / 10);

  runInThreadsAndWait({
      [&] {
        runInThreadsAndWait(
            nconsumers,
            [&](size_t /* k */) {
              size_t thReadsRemaining = writes / nconsumers;
              EventBase eb;
              EventHandlerMock eh(&eb, efd);
              eh.registerHandler(EventHandler::READ | EventHandler::PERSIST);
              EXPECT_CALL(eh, _handlerReady(_))
                  .WillRepeatedly(Invoke([&](uint16_t /* events */) {
                    nullptr_t val;
                    if (!queue.readIfNotEmpty(val)) {
                      return;
                    }
                    efd_read();
                    --readsRemaining;
                    if (--thReadsRemaining == 0) {
                      eh.unregisterHandler();
                    }
                  }));
              eb.loop();
            });
      },
      [&] {
        runInThreadsAndWait(nproducers,
                            [&](size_t /* k */) {
                              for (size_t i = 0; i < writes / nproducers; ++i) {
                                this_thread::sleep_for(chrono::milliseconds(1));
                                queue.blockingWrite(nullptr);
                                efd_write(1);
                                --writesRemaining;
                              }
                            });
      },
  });

  EXPECT_EQ(0, writesRemaining);
  EXPECT_EQ(0, readsRemaining);
}
