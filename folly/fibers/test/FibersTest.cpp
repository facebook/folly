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
#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <folly/Benchmark.h>
#include <folly/Memory.h>
#include <folly/futures/Future.h>

#include <folly/fibers/AddTasks.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/fibers/FiberManager.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/fibers/GenericBaton.h>
#include <folly/fibers/SimpleLoopController.h>
#include <folly/fibers/WhenN.h>

using namespace folly::fibers;

using folly::Try;

TEST(FiberManager, batonTimedWaitTimeout) {
  bool taskAdded = false;
  size_t iterations = 0;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        Baton baton;

        auto res = baton.timed_wait(std::chrono::milliseconds(230));

        EXPECT_FALSE(res);
        EXPECT_EQ(5, iterations);

        loopController.stop();
      });
      manager.addTask([&]() {
        Baton baton;

        auto res = baton.timed_wait(std::chrono::milliseconds(130));

        EXPECT_FALSE(res);
        EXPECT_EQ(3, iterations);

        loopController.stop();
      });
      taskAdded = true;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      iterations++;
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, batonTimedWaitPost) {
  bool taskAdded = false;
  size_t iterations = 0;
  Baton* baton_ptr;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        Baton baton;
        baton_ptr = &baton;

        auto res = baton.timed_wait(std::chrono::milliseconds(130));

        EXPECT_TRUE(res);
        EXPECT_EQ(2, iterations);

        loopController.stop();
      });
      taskAdded = true;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      iterations++;
      if (iterations == 2) {
        baton_ptr->post();
      }
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, batonTimedWaitTimeoutEvb) {
  size_t tasksComplete = 0;

  folly::EventBase evb;

  FiberManager manager(folly::make_unique<EventBaseLoopController>());
  dynamic_cast<EventBaseLoopController&>(manager.loopController())
      .attachEventBase(evb);

  auto task = [&](size_t timeout_ms) {
    Baton baton;

    auto start = EventBaseLoopController::Clock::now();
    auto res = baton.timed_wait(std::chrono::milliseconds(timeout_ms));
    auto finish = EventBaseLoopController::Clock::now();

    EXPECT_FALSE(res);

    auto duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);

    EXPECT_GT(duration_ms.count(), timeout_ms - 50);
    EXPECT_LT(duration_ms.count(), timeout_ms + 50);

    if (++tasksComplete == 2) {
      evb.terminateLoopSoon();
    }
  };

  evb.runInEventBaseThread([&]() {
    manager.addTask([&]() { task(500); });
    manager.addTask([&]() { task(250); });
  });

  evb.loopForever();

  EXPECT_EQ(2, tasksComplete);
}

TEST(FiberManager, batonTimedWaitPostEvb) {
  size_t tasksComplete = 0;

  folly::EventBase evb;

  FiberManager manager(folly::make_unique<EventBaseLoopController>());
  dynamic_cast<EventBaseLoopController&>(manager.loopController())
      .attachEventBase(evb);

  evb.runInEventBaseThread([&]() {
    manager.addTask([&]() {
      Baton baton;

      evb.tryRunAfterDelay([&]() { baton.post(); }, 100);

      auto start = EventBaseLoopController::Clock::now();
      auto res = baton.timed_wait(std::chrono::milliseconds(130));
      auto finish = EventBaseLoopController::Clock::now();

      EXPECT_TRUE(res);

      auto duration_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);

      EXPECT_TRUE(duration_ms.count() > 95 && duration_ms.count() < 110);

      if (++tasksComplete == 1) {
        evb.terminateLoopSoon();
      }
    });
  });

  evb.loopForever();

  EXPECT_EQ(1, tasksComplete);
}

TEST(FiberManager, batonTryWait) {
  FiberManager manager(folly::make_unique<SimpleLoopController>());

  // Check if try_wait and post work as expected
  Baton b;

  manager.addTask([&]() {
    while (!b.try_wait()) {
    }
  });
  auto thr = std::thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    b.post();
  });

  manager.loopUntilNoReady();
  thr.join();

  Baton c;

  // Check try_wait without post
  manager.addTask([&]() {
    int cnt = 100;
    while (cnt && !c.try_wait()) {
      cnt--;
    }
    EXPECT_TRUE(!c.try_wait()); // must still hold
    EXPECT_EQ(cnt, 0);
  });

  manager.loopUntilNoReady();
}

TEST(FiberManager, genericBatonFiberWait) {
  FiberManager manager(folly::make_unique<SimpleLoopController>());

  GenericBaton b;
  bool fiberRunning = false;

  manager.addTask([&]() {
    EXPECT_EQ(manager.hasActiveFiber(), true);
    fiberRunning = true;
    b.wait();
    fiberRunning = false;
  });

  EXPECT_FALSE(fiberRunning);
  manager.loopUntilNoReady();
  EXPECT_TRUE(fiberRunning); // ensure fiber still active

  auto thr = std::thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    b.post();
  });

  while (fiberRunning) {
    manager.loopUntilNoReady();
  }

  thr.join();
}

TEST(FiberManager, genericBatonThreadWait) {
  FiberManager manager(folly::make_unique<SimpleLoopController>());
  GenericBaton b;
  std::atomic<bool> threadWaiting(false);

  auto thr = std::thread([&]() {
    threadWaiting = true;
    b.wait();
    threadWaiting = false;
  });

  while (!threadWaiting) {
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  manager.addTask([&]() {
    EXPECT_EQ(manager.hasActiveFiber(), true);
    EXPECT_TRUE(threadWaiting);
    b.post();
    while (threadWaiting) {
    }
  });

  manager.loopUntilNoReady();
  thr.join();
}

TEST(FiberManager, addTasksNoncopyable) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<std::unique_ptr<int>()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
            return folly::make_unique<int>(i * 2 + 1);
          });
        }

        auto iter = addTasks(funcs.begin(), funcs.end());

        size_t n = 0;
        while (iter.hasNext()) {
          auto result = iter.awaitNext();
          EXPECT_EQ(2 * iter.getTaskID() + 1, *result);
          EXPECT_GE(2 - n, pendingFibers.size());
          ++n;
        }
        EXPECT_EQ(3, n);
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, awaitThrow) {
  folly::EventBase evb;
  struct ExpectedException {};
  getFiberManager(evb)
      .addTaskFuture([&] {
        EXPECT_THROW(
          await([](Promise<int> p) {
              p.setValue(42);
              throw ExpectedException();
            }),
          ExpectedException
        );

        EXPECT_THROW(
          await([&](Promise<int> p) {
              evb.runInEventBaseThread([p = std::move(p)]() mutable {
                  p.setValue(42);
                });
              throw ExpectedException();
            }),
          ExpectedException);
      })
      .waitVia(&evb);
}

TEST(FiberManager, addTasksThrow) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<int()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
            if (i % 2 == 0) {
              throw std::runtime_error("Runtime");
            }
            return i * 2 + 1;
          });
        }

        auto iter = addTasks(funcs.begin(), funcs.end());

        size_t n = 0;
        while (iter.hasNext()) {
          try {
            int result = iter.awaitNext();
            EXPECT_EQ(1, iter.getTaskID() % 2);
            EXPECT_EQ(2 * iter.getTaskID() + 1, result);
          } catch (...) {
            EXPECT_EQ(0, iter.getTaskID() % 2);
          }
          EXPECT_GE(2 - n, pendingFibers.size());
          ++n;
        }
        EXPECT_EQ(3, n);
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, addTasksVoid) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<void()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
          });
        }

        auto iter = addTasks(funcs.begin(), funcs.end());

        size_t n = 0;
        while (iter.hasNext()) {
          iter.awaitNext();
          EXPECT_GE(2 - n, pendingFibers.size());
          ++n;
        }
        EXPECT_EQ(3, n);
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, addTasksVoidThrow) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<void()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
            if (i % 2 == 0) {
              throw std::runtime_error("");
            }
          });
        }

        auto iter = addTasks(funcs.begin(), funcs.end());

        size_t n = 0;
        while (iter.hasNext()) {
          try {
            iter.awaitNext();
            EXPECT_EQ(1, iter.getTaskID() % 2);
          } catch (...) {
            EXPECT_EQ(0, iter.getTaskID() % 2);
          }
          EXPECT_GE(2 - n, pendingFibers.size());
          ++n;
        }
        EXPECT_EQ(3, n);
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, addTasksReserve) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<void()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([&pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
          });
        }

        auto iter = addTasks(funcs.begin(), funcs.end());

        iter.reserve(2);
        EXPECT_TRUE(iter.hasCompleted());
        EXPECT_TRUE(iter.hasPending());
        EXPECT_TRUE(iter.hasNext());

        iter.awaitNext();
        EXPECT_TRUE(iter.hasCompleted());
        EXPECT_TRUE(iter.hasPending());
        EXPECT_TRUE(iter.hasNext());

        iter.awaitNext();
        EXPECT_FALSE(iter.hasCompleted());
        EXPECT_TRUE(iter.hasPending());
        EXPECT_TRUE(iter.hasNext());

        iter.awaitNext();
        EXPECT_FALSE(iter.hasCompleted());
        EXPECT_FALSE(iter.hasPending());
        EXPECT_FALSE(iter.hasNext());
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, addTaskDynamic) {
  folly::EventBase evb;

  Baton batons[3];

  auto makeTask = [&](size_t taskId) {
    return [&, taskId]() -> size_t {
      batons[taskId].wait();
      return taskId;
    };
  };

  getFiberManager(evb)
      .addTaskFuture([&]() {
        TaskIterator<size_t> iterator;

        iterator.addTask(makeTask(0));
        iterator.addTask(makeTask(1));

        batons[1].post();

        EXPECT_EQ(1, iterator.awaitNext());

        iterator.addTask(makeTask(2));

        batons[2].post();

        EXPECT_EQ(2, iterator.awaitNext());

        batons[0].post();

        EXPECT_EQ(0, iterator.awaitNext());
      })
      .waitVia(&evb);
}

TEST(FiberManager, forEach) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<int()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
            return i * 2 + 1;
          });
        }

        std::vector<std::pair<size_t, int>> results;
        forEach(funcs.begin(), funcs.end(), [&results](size_t id, int result) {
          results.emplace_back(id, result);
        });
        EXPECT_EQ(3, results.size());
        EXPECT_TRUE(pendingFibers.empty());
        for (size_t i = 0; i < 3; ++i) {
          EXPECT_EQ(results[i].first * 2 + 1, results[i].second);
        }
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, collectN) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<int()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
            return i * 2 + 1;
          });
        }

        auto results = collectN(funcs.begin(), funcs.end(), 2);
        EXPECT_EQ(2, results.size());
        EXPECT_EQ(1, pendingFibers.size());
        for (size_t i = 0; i < 2; ++i) {
          EXPECT_EQ(results[i].first * 2 + 1, results[i].second);
        }
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, collectNThrow) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<int()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
            throw std::runtime_error("Runtime");
            return i * 2 + 1;
          });
        }

        try {
          collectN(funcs.begin(), funcs.end(), 2);
        } catch (...) {
          EXPECT_EQ(1, pendingFibers.size());
        }
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, collectNVoid) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<void()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
          });
        }

        auto results = collectN(funcs.begin(), funcs.end(), 2);
        EXPECT_EQ(2, results.size());
        EXPECT_EQ(1, pendingFibers.size());
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, collectNVoidThrow) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<void()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
            throw std::runtime_error("Runtime");
          });
        }

        try {
          collectN(funcs.begin(), funcs.end(), 2);
        } catch (...) {
          EXPECT_EQ(1, pendingFibers.size());
        }
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, collectAll) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<int()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
            return i * 2 + 1;
          });
        }

        auto results = collectAll(funcs.begin(), funcs.end());
        EXPECT_TRUE(pendingFibers.empty());
        for (size_t i = 0; i < 3; ++i) {
          EXPECT_EQ(i * 2 + 1, results[i]);
        }
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, collectAllVoid) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<void()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
          });
        }

        collectAll(funcs.begin(), funcs.end());
        EXPECT_TRUE(pendingFibers.empty());
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, collectAny) {
  std::vector<Promise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask([&]() {
        std::vector<std::function<int()>> funcs;
        for (size_t i = 0; i < 3; ++i) {
          funcs.push_back([i, &pendingFibers]() {
            await([&pendingFibers](Promise<int> promise) {
              pendingFibers.push_back(std::move(promise));
            });
            if (i == 1) {
              throw std::runtime_error("This exception will be ignored");
            }
            return i * 2 + 1;
          });
        }

        auto result = collectAny(funcs.begin(), funcs.end());
        EXPECT_EQ(2, pendingFibers.size());
        EXPECT_EQ(2, result.first);
        EXPECT_EQ(2 * 2 + 1, result.second);
      });
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

namespace {
/* Checks that this function was run from a main context,
   by comparing an address on a stack to a known main stack address
   and a known related fiber stack address.  The assumption
   is that fiber stack and main stack will be far enough apart,
   while any two values on the same stack will be close. */
void expectMainContext(bool& ran, int* mainLocation, int* fiberLocation) {
  int here;
  /* 2 pages is a good guess */
  constexpr ssize_t DISTANCE = 0x2000 / sizeof(int);
  if (fiberLocation) {
    EXPECT_TRUE(std::abs(&here - fiberLocation) > DISTANCE);
  }
  if (mainLocation) {
    EXPECT_TRUE(std::abs(&here - mainLocation) < DISTANCE);
  }

  EXPECT_FALSE(ran);
  ran = true;
}
}

TEST(FiberManager, runInMainContext) {
  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  bool checkRan = false;

  int mainLocation;
  manager.runInMainContext(
      [&]() { expectMainContext(checkRan, &mainLocation, nullptr); });
  EXPECT_TRUE(checkRan);

  checkRan = false;

  manager.addTask([&]() {
    struct A {
      explicit A(int value_) : value(value_) {}
      A(const A&) = delete;
      A(A&&) = default;

      int value;
    };
    int stackLocation;
    auto ret = runInMainContext([&]() {
      expectMainContext(checkRan, &mainLocation, &stackLocation);
      return A(42);
    });
    EXPECT_TRUE(checkRan);
    EXPECT_EQ(42, ret.value);
  });

  loopController.loop([&]() { loopController.stop(); });

  EXPECT_TRUE(checkRan);
}

TEST(FiberManager, addTaskFinally) {
  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  bool checkRan = false;

  int mainLocation;

  manager.addTaskFinally(
      [&]() { return 1234; },
      [&](Try<int>&& result) {
        EXPECT_EQ(result.value(), 1234);

        expectMainContext(checkRan, &mainLocation, nullptr);
      });

  EXPECT_FALSE(checkRan);

  loopController.loop([&]() { loopController.stop(); });

  EXPECT_TRUE(checkRan);
}

TEST(FiberManager, fibersPoolWithinLimit) {
  FiberManager::Options opts;
  opts.maxFibersPoolSize = 5;

  FiberManager manager(folly::make_unique<SimpleLoopController>(), opts);
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  size_t fibersRun = 0;

  for (size_t i = 0; i < 5; ++i) {
    manager.addTask([&]() { ++fibersRun; });
  }
  loopController.loop([&]() { loopController.stop(); });

  EXPECT_EQ(5, fibersRun);
  EXPECT_EQ(5, manager.fibersAllocated());
  EXPECT_EQ(5, manager.fibersPoolSize());

  for (size_t i = 0; i < 5; ++i) {
    manager.addTask([&]() { ++fibersRun; });
  }
  loopController.loop([&]() { loopController.stop(); });

  EXPECT_EQ(10, fibersRun);
  EXPECT_EQ(5, manager.fibersAllocated());
  EXPECT_EQ(5, manager.fibersPoolSize());
}

TEST(FiberManager, fibersPoolOverLimit) {
  FiberManager::Options opts;
  opts.maxFibersPoolSize = 5;

  FiberManager manager(folly::make_unique<SimpleLoopController>(), opts);
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  size_t fibersRun = 0;

  for (size_t i = 0; i < 10; ++i) {
    manager.addTask([&]() { ++fibersRun; });
  }

  EXPECT_EQ(0, fibersRun);
  EXPECT_EQ(10, manager.fibersAllocated());
  EXPECT_EQ(0, manager.fibersPoolSize());

  loopController.loop([&]() { loopController.stop(); });

  EXPECT_EQ(10, fibersRun);
  EXPECT_EQ(5, manager.fibersAllocated());
  EXPECT_EQ(5, manager.fibersPoolSize());
}

TEST(FiberManager, remoteFiberBasic) {
  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  int result[2];
  result[0] = result[1] = 0;
  folly::Optional<Promise<int>> savedPromise[2];
  manager.addTask([&]() {
    result[0] = await(
        [&](Promise<int> promise) { savedPromise[0] = std::move(promise); });
  });
  manager.addTask([&]() {
    result[1] = await(
        [&](Promise<int> promise) { savedPromise[1] = std::move(promise); });
  });

  manager.loopUntilNoReady();

  EXPECT_TRUE(savedPromise[0].hasValue());
  EXPECT_TRUE(savedPromise[1].hasValue());
  EXPECT_EQ(0, result[0]);
  EXPECT_EQ(0, result[1]);

  std::thread remoteThread0{[&]() { savedPromise[0]->setValue(42); }};
  std::thread remoteThread1{[&]() { savedPromise[1]->setValue(43); }};
  remoteThread0.join();
  remoteThread1.join();
  EXPECT_EQ(0, result[0]);
  EXPECT_EQ(0, result[1]);
  /* Should only have scheduled once */
  EXPECT_EQ(1, loopController.remoteScheduleCalled());

  manager.loopUntilNoReady();
  EXPECT_EQ(42, result[0]);
  EXPECT_EQ(43, result[1]);
}

TEST(FiberManager, addTaskRemoteBasic) {
  FiberManager manager(folly::make_unique<SimpleLoopController>());

  int result[2];
  result[0] = result[1] = 0;
  folly::Optional<Promise<int>> savedPromise[2];

  std::thread remoteThread0{[&]() {
    manager.addTaskRemote([&]() {
      result[0] = await(
          [&](Promise<int> promise) { savedPromise[0] = std::move(promise); });
    });
  }};
  std::thread remoteThread1{[&]() {
    manager.addTaskRemote([&]() {
      result[1] = await(
          [&](Promise<int> promise) { savedPromise[1] = std::move(promise); });
    });
  }};
  remoteThread0.join();
  remoteThread1.join();

  manager.loopUntilNoReady();

  EXPECT_TRUE(savedPromise[0].hasValue());
  EXPECT_TRUE(savedPromise[1].hasValue());
  EXPECT_EQ(0, result[0]);
  EXPECT_EQ(0, result[1]);

  savedPromise[0]->setValue(42);
  savedPromise[1]->setValue(43);

  EXPECT_EQ(0, result[0]);
  EXPECT_EQ(0, result[1]);

  manager.loopUntilNoReady();
  EXPECT_EQ(42, result[0]);
  EXPECT_EQ(43, result[1]);
}

TEST(FiberManager, remoteHasTasks) {
  size_t counter = 0;
  FiberManager fm(folly::make_unique<SimpleLoopController>());
  std::thread remote([&]() { fm.addTaskRemote([&]() { ++counter; }); });

  remote.join();

  while (fm.hasTasks()) {
    fm.loopUntilNoReady();
  }

  EXPECT_FALSE(fm.hasTasks());
  EXPECT_EQ(counter, 1);
}

TEST(FiberManager, remoteHasReadyTasks) {
  int result = 0;
  folly::Optional<Promise<int>> savedPromise;
  FiberManager fm(folly::make_unique<SimpleLoopController>());
  std::thread remote([&]() {
    fm.addTaskRemote([&]() {
      result = await(
          [&](Promise<int> promise) { savedPromise = std::move(promise); });
      EXPECT_TRUE(fm.hasTasks());
    });
  });

  remote.join();
  EXPECT_TRUE(fm.hasTasks());

  fm.loopUntilNoReady();
  EXPECT_TRUE(fm.hasTasks());

  std::thread remote2([&]() { savedPromise->setValue(47); });
  remote2.join();
  EXPECT_TRUE(fm.hasTasks());

  fm.loopUntilNoReady();
  EXPECT_FALSE(fm.hasTasks());

  EXPECT_EQ(result, 47);
}

template <typename Data>
void testFiberLocal() {
  FiberManager fm(
      LocalType<Data>(), folly::make_unique<SimpleLoopController>());

  fm.addTask([]() {
    EXPECT_EQ(42, local<Data>().value);

    local<Data>().value = 43;

    addTask([]() {
      EXPECT_EQ(43, local<Data>().value);

      local<Data>().value = 44;

      addTask([]() { EXPECT_EQ(44, local<Data>().value); });
    });
  });

  fm.addTask([&]() {
    EXPECT_EQ(42, local<Data>().value);

    local<Data>().value = 43;

    fm.addTaskRemote([]() { EXPECT_EQ(43, local<Data>().value); });
  });

  fm.addTask([]() {
    EXPECT_EQ(42, local<Data>().value);
    local<Data>().value = 43;

    auto task = []() {
      EXPECT_EQ(43, local<Data>().value);
      local<Data>().value = 44;
    };
    std::vector<std::function<void()>> tasks{task};
    collectAny(tasks.begin(), tasks.end());

    EXPECT_EQ(43, local<Data>().value);
  });

  fm.loopUntilNoReady();
  EXPECT_FALSE(fm.hasTasks());
}

TEST(FiberManager, fiberLocal) {
  struct SimpleData {
    int value{42};
  };

  testFiberLocal<SimpleData>();
}

TEST(FiberManager, fiberLocalHeap) {
  struct LargeData {
    char _[1024 * 1024];
    int value{42};
  };

  testFiberLocal<LargeData>();
}

TEST(FiberManager, fiberLocalDestructor) {
  struct CrazyData {
    size_t data{42};

    ~CrazyData() {
      if (data == 41) {
        addTask([]() {
          EXPECT_EQ(42, local<CrazyData>().data);
          // Make sure we don't have infinite loop
          local<CrazyData>().data = 0;
        });
      }
    }
  };

  FiberManager fm(
      LocalType<CrazyData>(), folly::make_unique<SimpleLoopController>());

  fm.addTask([]() { local<CrazyData>().data = 41; });

  fm.loopUntilNoReady();
  EXPECT_FALSE(fm.hasTasks());
}

TEST(FiberManager, yieldTest) {
  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(manager.loopController());

  bool checkRan = false;

  manager.addTask([&]() {
    manager.yield();
    checkRan = true;
  });

  loopController.loop([&]() {
    if (checkRan) {
      loopController.stop();
    }
  });

  EXPECT_TRUE(checkRan);
}

TEST(FiberManager, RequestContext) {
  FiberManager fm(folly::make_unique<SimpleLoopController>());

  bool checkRun1 = false;
  bool checkRun2 = false;
  bool checkRun3 = false;
  bool checkRun4 = false;
  folly::fibers::Baton baton1;
  folly::fibers::Baton baton2;
  folly::fibers::Baton baton3;
  folly::fibers::Baton baton4;

  folly::RequestContext::create();
  auto rcontext1 = folly::RequestContext::get();
  fm.addTask([&]() {
    EXPECT_EQ(rcontext1, folly::RequestContext::get());
    baton1.wait([&]() { EXPECT_EQ(rcontext1, folly::RequestContext::get()); });
    EXPECT_EQ(rcontext1, folly::RequestContext::get());
    runInMainContext(
        [&]() { EXPECT_EQ(rcontext1, folly::RequestContext::get()); });
    checkRun1 = true;
  });

  folly::RequestContext::create();
  auto rcontext2 = folly::RequestContext::get();
  fm.addTaskRemote([&]() {
    EXPECT_EQ(rcontext2, folly::RequestContext::get());
    baton2.wait();
    EXPECT_EQ(rcontext2, folly::RequestContext::get());
    checkRun2 = true;
  });

  folly::RequestContext::create();
  auto rcontext3 = folly::RequestContext::get();
  fm.addTaskFinally(
      [&]() {
        EXPECT_EQ(rcontext3, folly::RequestContext::get());
        baton3.wait();
        EXPECT_EQ(rcontext3, folly::RequestContext::get());

        return folly::Unit();
      },
      [&](Try<folly::Unit>&& /* t */) {
        EXPECT_EQ(rcontext3, folly::RequestContext::get());
        checkRun3 = true;
      });

  folly::RequestContext::setContext(nullptr);
  fm.addTask([&]() {
    folly::RequestContext::create();
    auto rcontext4 = folly::RequestContext::get();
    baton4.wait();
    EXPECT_EQ(rcontext4, folly::RequestContext::get());
    checkRun4 = true;
  });

  folly::RequestContext::create();
  auto rcontext = folly::RequestContext::get();

  fm.loopUntilNoReady();
  EXPECT_EQ(rcontext, folly::RequestContext::get());

  baton1.post();
  EXPECT_EQ(rcontext, folly::RequestContext::get());
  fm.loopUntilNoReady();
  EXPECT_TRUE(checkRun1);
  EXPECT_EQ(rcontext, folly::RequestContext::get());

  baton2.post();
  EXPECT_EQ(rcontext, folly::RequestContext::get());
  fm.loopUntilNoReady();
  EXPECT_TRUE(checkRun2);
  EXPECT_EQ(rcontext, folly::RequestContext::get());

  baton3.post();
  EXPECT_EQ(rcontext, folly::RequestContext::get());
  fm.loopUntilNoReady();
  EXPECT_TRUE(checkRun3);
  EXPECT_EQ(rcontext, folly::RequestContext::get());

  baton4.post();
  EXPECT_EQ(rcontext, folly::RequestContext::get());
  fm.loopUntilNoReady();
  EXPECT_TRUE(checkRun4);
  EXPECT_EQ(rcontext, folly::RequestContext::get());
}

TEST(FiberManager, resizePeriodically) {
  FiberManager::Options opts;
  opts.fibersPoolResizePeriodMs = 300;
  opts.maxFibersPoolSize = 5;

  FiberManager manager(folly::make_unique<EventBaseLoopController>(), opts);

  folly::EventBase evb;
  dynamic_cast<EventBaseLoopController&>(manager.loopController())
      .attachEventBase(evb);

  std::vector<Baton> batons(10);

  size_t tasksRun = 0;
  for (size_t i = 0; i < 30; ++i) {
    manager.addTask([i, &batons, &tasksRun]() {
      ++tasksRun;
      // Keep some fibers active indefinitely
      if (i < batons.size()) {
        batons[i].wait();
      }
    });
  }

  EXPECT_EQ(0, tasksRun);
  EXPECT_EQ(30, manager.fibersAllocated());
  EXPECT_EQ(0, manager.fibersPoolSize());

  evb.loopOnce();
  EXPECT_EQ(30, tasksRun);
  EXPECT_EQ(30, manager.fibersAllocated());
  // Can go over maxFibersPoolSize, 10 of 30 fibers still active
  EXPECT_EQ(20, manager.fibersPoolSize());

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  evb.loopOnce(); // no fibers active in this period
  EXPECT_EQ(30, manager.fibersAllocated());
  EXPECT_EQ(20, manager.fibersPoolSize());

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  evb.loopOnce(); // should shrink fibers pool to maxFibersPoolSize
  EXPECT_EQ(15, manager.fibersAllocated());
  EXPECT_EQ(5, manager.fibersPoolSize());

  for (size_t i = 0; i < batons.size(); ++i) {
    batons[i].post();
  }
  evb.loopOnce();
  EXPECT_EQ(15, manager.fibersAllocated());
  EXPECT_EQ(15, manager.fibersPoolSize());

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  evb.loopOnce(); // 10 fibers active in last period
  EXPECT_EQ(10, manager.fibersAllocated());
  EXPECT_EQ(10, manager.fibersPoolSize());

  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  evb.loopOnce();
  EXPECT_EQ(5, manager.fibersAllocated());
  EXPECT_EQ(5, manager.fibersPoolSize());
}

TEST(FiberManager, batonWaitTimeoutHandler) {
  FiberManager manager(folly::make_unique<EventBaseLoopController>());

  folly::EventBase evb;
  dynamic_cast<EventBaseLoopController&>(manager.loopController())
      .attachEventBase(evb);

  size_t fibersRun = 0;
  Baton baton;
  Baton::TimeoutHandler timeoutHandler;

  manager.addTask([&]() {
    baton.wait(timeoutHandler);
    ++fibersRun;
  });
  manager.loopUntilNoReady();

  EXPECT_FALSE(baton.try_wait());
  EXPECT_EQ(0, fibersRun);

  timeoutHandler.scheduleTimeout(std::chrono::milliseconds(250));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_FALSE(baton.try_wait());
  EXPECT_EQ(0, fibersRun);

  evb.loopOnce();
  manager.loopUntilNoReady();

  EXPECT_EQ(1, fibersRun);
}

TEST(FiberManager, batonWaitTimeoutMany) {
  FiberManager manager(folly::make_unique<EventBaseLoopController>());

  folly::EventBase evb;
  dynamic_cast<EventBaseLoopController&>(manager.loopController())
      .attachEventBase(evb);

  constexpr size_t kNumTimeoutTasks = 10000;
  size_t tasksCount = kNumTimeoutTasks;

  // We add many tasks to hit timeout queue deallocation logic.
  for (size_t i = 0; i < kNumTimeoutTasks; ++i) {
    manager.addTask([&]() {
      Baton baton;
      Baton::TimeoutHandler timeoutHandler;

      folly::fibers::addTask([&] {
        timeoutHandler.scheduleTimeout(std::chrono::milliseconds(1000));
      });

      baton.wait(timeoutHandler);
      if (--tasksCount == 0) {
        evb.terminateLoopSoon();
      }
    });
  }

  evb.loopForever();
}

TEST(FiberManager, remoteFutureTest) {
  FiberManager fiberManager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(fiberManager.loopController());

  int testValue1 = 5;
  int testValue2 = 7;
  auto f1 = fiberManager.addTaskFuture([&]() { return testValue1; });
  auto f2 = fiberManager.addTaskRemoteFuture([&]() { return testValue2; });
  loopController.loop([&]() { loopController.stop(); });
  auto v1 = f1.get();
  auto v2 = f2.get();

  EXPECT_EQ(v1, testValue1);
  EXPECT_EQ(v2, testValue2);
}

// Test that a void function produes a Future<Unit>.
TEST(FiberManager, remoteFutureVoidUnitTest) {
  FiberManager fiberManager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(fiberManager.loopController());

  bool ranLocal = false;
  folly::Future<folly::Unit> futureLocal =
      fiberManager.addTaskFuture([&]() { ranLocal = true; });

  bool ranRemote = false;
  folly::Future<folly::Unit> futureRemote =
      fiberManager.addTaskRemoteFuture([&]() { ranRemote = true; });

  loopController.loop([&]() { loopController.stop(); });

  futureLocal.wait();
  ASSERT_TRUE(ranLocal);

  futureRemote.wait();
  ASSERT_TRUE(ranRemote);
}

TEST(FiberManager, nestedFiberManagers) {
  folly::EventBase outerEvb;
  folly::EventBase innerEvb;

  getFiberManager(outerEvb).addTask([&]() {
    EXPECT_EQ(
        &getFiberManager(outerEvb), FiberManager::getFiberManagerUnsafe());

    runInMainContext([&]() {
      getFiberManager(innerEvb).addTask([&]() {
        EXPECT_EQ(
            &getFiberManager(innerEvb), FiberManager::getFiberManagerUnsafe());

        innerEvb.terminateLoopSoon();
      });

      innerEvb.loopForever();
    });

    EXPECT_EQ(
        &getFiberManager(outerEvb), FiberManager::getFiberManagerUnsafe());

    outerEvb.terminateLoopSoon();
  });

  outerEvb.loopForever();
}

static size_t sNumAwaits;

void runBenchmark(size_t numAwaits, size_t toSend) {
  sNumAwaits = numAwaits;

  FiberManager fiberManager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
      dynamic_cast<SimpleLoopController&>(fiberManager.loopController());

  std::queue<Promise<int>> pendingRequests;
  static const size_t maxOutstanding = 5;

  auto loop = [&fiberManager, &loopController, &pendingRequests, &toSend]() {
    if (pendingRequests.size() == maxOutstanding || toSend == 0) {
      if (pendingRequests.empty()) {
        return;
      }
      pendingRequests.front().setValue(0);
      pendingRequests.pop();
    } else {
      fiberManager.addTask([&pendingRequests]() {
        for (size_t i = 0; i < sNumAwaits; ++i) {
          auto result = await([&pendingRequests](Promise<int> promise) {
            pendingRequests.push(std::move(promise));
          });
          DCHECK_EQ(result, 0);
        }
      });

      if (--toSend == 0) {
        loopController.stop();
      }
    }
  };

  loopController.loop(std::move(loop));
}

BENCHMARK(FiberManagerBasicOneAwait, iters) {
  runBenchmark(1, iters);
}

BENCHMARK(FiberManagerBasicFiveAwaits, iters) {
  runBenchmark(5, iters);
}

BENCHMARK(FiberManagerCreateDestroy, iters) {
  for (size_t i = 0; i < iters; ++i) {
    folly::EventBase evb;
    auto& fm = folly::fibers::getFiberManager(evb);
    fm.addTask([]() {});
    evb.loop();
  }
}

BENCHMARK(FiberManagerAllocateDeallocatePattern, iters) {
  static const size_t kNumAllocations = 10000;

  FiberManager::Options opts;
  opts.maxFibersPoolSize = 0;

  FiberManager fiberManager(folly::make_unique<SimpleLoopController>(), opts);

  for (size_t iter = 0; iter < iters; ++iter) {
    EXPECT_EQ(0, fiberManager.fibersPoolSize());

    size_t fibersRun = 0;

    for (size_t i = 0; i < kNumAllocations; ++i) {
      fiberManager.addTask([&fibersRun] { ++fibersRun; });
      fiberManager.loopUntilNoReady();
    }

    EXPECT_EQ(10000, fibersRun);
    EXPECT_EQ(0, fiberManager.fibersPoolSize());
  }
}

BENCHMARK(FiberManagerAllocateLargeChunk, iters) {
  static const size_t kNumAllocations = 10000;

  FiberManager::Options opts;
  opts.maxFibersPoolSize = 0;

  FiberManager fiberManager(folly::make_unique<SimpleLoopController>(), opts);

  for (size_t iter = 0; iter < iters; ++iter) {
    EXPECT_EQ(0, fiberManager.fibersPoolSize());

    size_t fibersRun = 0;

    for (size_t i = 0; i < kNumAllocations; ++i) {
      fiberManager.addTask([&fibersRun] { ++fibersRun; });
    }

    fiberManager.loopUntilNoReady();

    EXPECT_EQ(10000, fibersRun);
    EXPECT_EQ(0, fiberManager.fibersPoolSize());
  }
}
