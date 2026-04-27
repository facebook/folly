/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/logging/LoggerDB.h>

#include <atomic>
#include <thread>

#include <folly/logging/Logger.h>
#include <folly/logging/test/TestLogHandler.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(LoggerDB, lookupNameCanonicalization) {
  LoggerDB db{LoggerDB::TESTING};
  Logger foo{&db, "foo"};
  Logger foo2{&db, "..foo.."};
  EXPECT_EQ(foo.getCategory(), foo2.getCategory());

  Logger fooBar{&db, "foo.bar"};
  Logger fooBar2{&db, ".foo..bar"};
  EXPECT_EQ(fooBar.getCategory(), fooBar2.getCategory());
}

TEST(LoggerDB, getCategory) {
  LoggerDB db{LoggerDB::TESTING};
}

TEST(LoggerDB, flushAllHandlers) {
  LoggerDB db{LoggerDB::TESTING};
  auto* cat1 = db.getCategory("foo");
  auto* cat2 = db.getCategory("foo.bar.test");
  auto* cat3 = db.getCategory("hello.world");
  auto* cat4 = db.getCategory("other.category");

  auto h1 = std::make_shared<TestLogHandler>();
  auto h2 = std::make_shared<TestLogHandler>();
  auto h3 = std::make_shared<TestLogHandler>();

  cat1->addHandler(h1);

  cat2->addHandler(h2);
  cat2->addHandler(h3);

  cat3->addHandler(h1);
  cat3->addHandler(h2);
  cat3->addHandler(h3);

  cat4->addHandler(h1);

  EXPECT_EQ(0, h1->getFlushCount());
  EXPECT_EQ(0, h2->getFlushCount());
  EXPECT_EQ(0, h3->getFlushCount());

  // Calling flushAllHandlers() should only flush each handler once,
  // even when they are attached to multiple categories.
  db.flushAllHandlers();
  EXPECT_EQ(1, h1->getFlushCount());
  EXPECT_EQ(1, h2->getFlushCount());
  EXPECT_EQ(1, h3->getFlushCount());

  db.flushAllHandlers();
  EXPECT_EQ(2, h1->getFlushCount());
  EXPECT_EQ(2, h2->getFlushCount());
  EXPECT_EQ(2, h3->getFlushCount());
}

TEST(LoggerDB, flushAllHandlersReturnValue) {
  LoggerDB db{LoggerDB::TESTING};
  auto* cat1 = db.getCategory("foo");
  auto* cat2 = db.getCategory("bar");

  // No handlers registered yet
  EXPECT_EQ(0, db.flushAllHandlers());

  auto h1 = std::make_shared<TestLogHandler>();
  auto h2 = std::make_shared<TestLogHandler>();
  cat1->addHandler(h1);
  cat2->addHandler(h2);

  // Two distinct handlers
  EXPECT_EQ(2, db.flushAllHandlers());

  // Register h1 on cat2 as well — should still only count 2 unique handlers
  cat2->addHandler(h1);
  EXPECT_EQ(2, db.flushAllHandlers());
}

TEST(LoggerDB, flushAllHandlersConcurrentWithReads) {
  // Verify that flushAllHandlers() does not block concurrent read operations
  // on the logger map. Both flushAllHandlers() and getCategoryOrNull() use
  // rlock(), so they can proceed concurrently without blocking each other.
  // If flushAllHandlers() incorrectly used wlock(), this test would exhibit
  // significant contention with the concurrent flush threads.
  LoggerDB db{LoggerDB::TESTING};
  auto* cat = db.getCategory("flush.test");
  auto handler = std::make_shared<TestLogHandler>();
  cat->addHandler(handler);

  constexpr int kIterations = 1000;
  std::atomic<bool> stop{false};

  // Reader thread: continuously looks up an existing category using
  // getCategoryOrNull(), which also acquires rlock(). With the correct
  // rlock() in flushAllHandlers(), both can run concurrently.
  std::thread reader([&]() {
    while (!stop.load(std::memory_order_relaxed)) {
      auto* c = db.getCategoryOrNull("flush.test");
      EXPECT_NE(nullptr, c);
    }
  });

  // Main thread: flush repeatedly.
  for (int i = 0; i < kIterations; ++i) {
    EXPECT_EQ(1, db.flushAllHandlers());
  }

  stop.store(true, std::memory_order_relaxed);
  reader.join();

  EXPECT_EQ(kIterations, handler->getFlushCount());
}

TEST(LoggerDB, contextCallbackSingle) {
  LoggerDB db{LoggerDB::TESTING};

  // Before any callback is registered, getContextString() should return empty.
  EXPECT_EQ("", db.getContextString());

  // Add a callback that returns a known string.
  db.addContextCallback([] { return std::string("hello"); });

  // getContextString() prepends a space to each non-empty callback result.
  EXPECT_EQ(" hello", db.getContextString());
}

TEST(LoggerDB, contextCallbackMultiple) {
  LoggerDB db{LoggerDB::TESTING};

  db.addContextCallback([] { return std::string("alpha"); });
  db.addContextCallback([] { return std::string("beta"); });
  db.addContextCallback([] { return std::string("gamma"); });

  // Each callback result is space-prefixed and appended in order.
  EXPECT_EQ(" alpha beta gamma", db.getContextString());
}

TEST(LoggerDB, contextCallbackConcurrentAddAndRead) {
  // Smoke test for concurrent addContextCallback + getContextString.
  // The release/acquire fix on callbacks_ ensures that a reader on a
  // weakly-ordered architecture (ARM) sees fully-visible memory backing
  // a newly-allocated CallbacksObj when it loads the non-null pointer.
  // On x86 (TSO), relaxed stores behave like release stores, so this
  // test cannot deterministically catch the ordering bug -- it requires
  // ARM hardware or TSAN to surface the race. We stress many fresh
  // LoggerDB instances to maximize the chance of hitting the null-to-
  // non-null transition window concurrently.
  constexpr int kInstances = 200;

  for (int i = 0; i < kInstances; ++i) {
    LoggerDB db{LoggerDB::TESTING};
    std::atomic<bool> ready{false};
    std::atomic<bool> stop{false};

    // Reader thread: spins on getContextString() until told to stop.
    std::thread reader([&]() {
      // Signal that we are actively reading.
      ready.store(true, std::memory_order_release);
      while (!stop.load(std::memory_order_relaxed)) {
        auto ctx = db.getContextString();
        // Must be either empty (callback not yet visible) or correct.
        if (!ctx.empty()) {
          EXPECT_NE(std::string::npos, ctx.find("val"));
        }
      }
    });

    // Wait for reader to be running before adding the callback,
    // maximizing the chance of concurrent null->non-null observation.
    while (!ready.load(std::memory_order_acquire)) {
      // spin
    }

    db.addContextCallback([] { return std::string("val"); });
    EXPECT_EQ(" val", db.getContextString());

    stop.store(true, std::memory_order_relaxed);
    reader.join();
  }
}
