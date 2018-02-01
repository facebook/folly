/*
 * Copyright 2017-present Facebook, Inc.
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

#include <memory>
#include <thread>

#include <folly/Exception.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/FutureExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/ThreadPoolExecutor.h>
#include <folly/executors/task_queue/LifoSemMPMCQueue.h>
#include <folly/executors/task_queue/UnboundedBlockingQueue.h>
#include <folly/executors/thread_factory/PriorityThreadFactory.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace std::chrono;

static Func burnMs(uint64_t ms) {
  return [ms]() { std::this_thread::sleep_for(milliseconds(ms)); };
}

template <class TPE>
static void basic() {
  // Create and destroy
  TPE tpe(10);
}

TEST(ThreadPoolExecutorTest, CPUBasic) {
  basic<CPUThreadPoolExecutor>();
}

TEST(IOThreadPoolExecutorTest, IOBasic) {
  basic<IOThreadPoolExecutor>();
}

template <class TPE>
static void resize() {
  TPE tpe(100);
  EXPECT_EQ(100, tpe.numThreads());
  tpe.setNumThreads(50);
  EXPECT_EQ(50, tpe.numThreads());
  tpe.setNumThreads(150);
  EXPECT_EQ(150, tpe.numThreads());
}

TEST(ThreadPoolExecutorTest, CPUResize) {
  resize<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOResize) {
  resize<IOThreadPoolExecutor>();
}

template <class TPE>
static void stop() {
  TPE tpe(1);
  std::atomic<int> completed(0);
  auto f = [&]() {
    burnMs(10)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.stop();
  EXPECT_GT(1000, completed);
}

// IOThreadPoolExecutor's stop() behaves like join(). Outstanding tasks belong
// to the event base, will be executed upon its destruction, and cannot be
// taken back.
template <>
void stop<IOThreadPoolExecutor>() {
  IOThreadPoolExecutor tpe(1);
  std::atomic<int> completed(0);
  auto f = [&]() {
    burnMs(10)();
    completed++;
  };
  for (int i = 0; i < 10; i++) {
    tpe.add(f);
  }
  tpe.stop();
  EXPECT_EQ(10, completed);
}

TEST(ThreadPoolExecutorTest, CPUStop) {
  stop<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOStop) {
  stop<IOThreadPoolExecutor>();
}

template <class TPE>
static void join() {
  TPE tpe(10);
  std::atomic<int> completed(0);
  auto f = [&]() {
    burnMs(1)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.join();
  EXPECT_EQ(1000, completed);
}

TEST(ThreadPoolExecutorTest, CPUJoin) {
  join<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOJoin) {
  join<IOThreadPoolExecutor>();
}

template <class TPE>
static void resizeUnderLoad() {
  TPE tpe(10);
  std::atomic<int> completed(0);
  auto f = [&]() {
    burnMs(1)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.setNumThreads(5);
  tpe.setNumThreads(15);
  tpe.join();
  EXPECT_EQ(1000, completed);
}

TEST(ThreadPoolExecutorTest, CPUResizeUnderLoad) {
  resizeUnderLoad<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOResizeUnderLoad) {
  resizeUnderLoad<IOThreadPoolExecutor>();
}

template <class TPE>
static void poolStats() {
  folly::Baton<> startBaton, endBaton;
  TPE tpe(1);
  auto stats = tpe.getPoolStats();
  EXPECT_EQ(1, stats.threadCount);
  EXPECT_EQ(1, stats.idleThreadCount);
  EXPECT_EQ(0, stats.activeThreadCount);
  EXPECT_EQ(0, stats.pendingTaskCount);
  EXPECT_EQ(0, tpe.getPendingTaskCount());
  EXPECT_EQ(0, stats.totalTaskCount);
  tpe.add([&]() {
    startBaton.post();
    endBaton.wait();
  });
  tpe.add([&]() {});
  startBaton.wait();
  stats = tpe.getPoolStats();
  EXPECT_EQ(1, stats.threadCount);
  EXPECT_EQ(0, stats.idleThreadCount);
  EXPECT_EQ(1, stats.activeThreadCount);
  EXPECT_EQ(1, stats.pendingTaskCount);
  EXPECT_EQ(1, tpe.getPendingTaskCount());
  EXPECT_EQ(2, stats.totalTaskCount);
  endBaton.post();
}

TEST(ThreadPoolExecutorTest, CPUPoolStats) {
  poolStats<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOPoolStats) {
  poolStats<IOThreadPoolExecutor>();
}

template <class TPE>
static void taskStats() {
  TPE tpe(1);
  std::atomic<int> c(0);
  tpe.subscribeToTaskStats([&](ThreadPoolExecutor::TaskStats stats) {
    int i = c++;
    EXPECT_LT(milliseconds(0), stats.runTime);
    if (i == 1) {
      EXPECT_LT(milliseconds(0), stats.waitTime);
    }
  });
  tpe.add(burnMs(10));
  tpe.add(burnMs(10));
  tpe.join();
  EXPECT_EQ(2, c);
}

TEST(ThreadPoolExecutorTest, CPUTaskStats) {
  taskStats<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOTaskStats) {
  taskStats<IOThreadPoolExecutor>();
}

template <class TPE>
static void expiration() {
  TPE tpe(1);
  std::atomic<int> statCbCount(0);
  tpe.subscribeToTaskStats([&](ThreadPoolExecutor::TaskStats stats) {
    int i = statCbCount++;
    if (i == 0) {
      EXPECT_FALSE(stats.expired);
    } else if (i == 1) {
      EXPECT_TRUE(stats.expired);
    } else {
      FAIL();
    }
  });
  std::atomic<int> expireCbCount(0);
  auto expireCb = [&]() { expireCbCount++; };
  tpe.add(burnMs(10), seconds(60), expireCb);
  tpe.add(burnMs(10), milliseconds(10), expireCb);
  tpe.join();
  EXPECT_EQ(2, statCbCount);
  EXPECT_EQ(1, expireCbCount);
}

TEST(ThreadPoolExecutorTest, CPUExpiration) {
  expiration<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOExpiration) {
  expiration<IOThreadPoolExecutor>();
}

template <typename TPE>
static void futureExecutor() {
  FutureExecutor<TPE> fe(2);
  std::atomic<int> c{0};
  fe.addFuture([]() { return makeFuture<int>(42); }).then([&](Try<int>&& t) {
    c++;
    EXPECT_EQ(42, t.value());
  });
  fe.addFuture([]() { return 100; }).then([&](Try<int>&& t) {
    c++;
    EXPECT_EQ(100, t.value());
  });
  fe.addFuture([]() { return makeFuture(); }).then([&](Try<Unit>&& t) {
    c++;
    EXPECT_NO_THROW(t.value());
  });
  fe.addFuture([]() { return; }).then([&](Try<Unit>&& t) {
    c++;
    EXPECT_NO_THROW(t.value());
  });
  fe.addFuture([]() { throw std::runtime_error("oops"); })
      .then([&](Try<Unit>&& t) {
        c++;
        EXPECT_THROW(t.value(), std::runtime_error);
      });
  // Test doing actual async work
  folly::Baton<> baton;
  fe.addFuture([&]() {
      auto p = std::make_shared<Promise<int>>();
      std::thread t([p]() {
        burnMs(10)();
        p->setValue(42);
      });
      t.detach();
      return p->getFuture();
    })
      .then([&](Try<int>&& t) {
        EXPECT_EQ(42, t.value());
        c++;
        baton.post();
      });
  baton.wait();
  fe.join();
  EXPECT_EQ(6, c);
}

TEST(ThreadPoolExecutorTest, CPUFuturePool) {
  futureExecutor<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOFuturePool) {
  futureExecutor<IOThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, PriorityPreemptionTest) {
  bool tookLopri = false;
  auto completed = 0;
  auto hipri = [&] {
    EXPECT_FALSE(tookLopri);
    completed++;
  };
  auto lopri = [&] {
    tookLopri = true;
    completed++;
  };
  CPUThreadPoolExecutor pool(0, 2);
  for (int i = 0; i < 50; i++) {
    pool.addWithPriority(lopri, Executor::LO_PRI);
  }
  for (int i = 0; i < 50; i++) {
    pool.addWithPriority(hipri, Executor::HI_PRI);
  }
  pool.setNumThreads(1);
  pool.join();
  EXPECT_EQ(100, completed);
}

class TestObserver : public ThreadPoolExecutor::Observer {
 public:
  void threadStarted(ThreadPoolExecutor::ThreadHandle*) override {
    threads_++;
  }
  void threadStopped(ThreadPoolExecutor::ThreadHandle*) override {
    threads_--;
  }
  void threadPreviouslyStarted(ThreadPoolExecutor::ThreadHandle*) override {
    threads_++;
  }
  void threadNotYetStopped(ThreadPoolExecutor::ThreadHandle*) override {
    threads_--;
  }
  void checkCalls() {
    ASSERT_EQ(threads_, 0);
  }

 private:
  std::atomic<int> threads_{0};
};

TEST(ThreadPoolExecutorTest, IOObserver) {
  auto observer = std::make_shared<TestObserver>();

  {
    IOThreadPoolExecutor exe(10);
    exe.addObserver(observer);
    exe.setNumThreads(3);
    exe.setNumThreads(0);
    exe.setNumThreads(7);
    exe.removeObserver(observer);
    exe.setNumThreads(10);
  }

  observer->checkCalls();
}

TEST(ThreadPoolExecutorTest, CPUObserver) {
  auto observer = std::make_shared<TestObserver>();

  {
    CPUThreadPoolExecutor exe(10);
    exe.addObserver(observer);
    exe.setNumThreads(3);
    exe.setNumThreads(0);
    exe.setNumThreads(7);
    exe.removeObserver(observer);
    exe.setNumThreads(10);
  }

  observer->checkCalls();
}

TEST(ThreadPoolExecutorTest, AddWithPriority) {
  std::atomic_int c{0};
  auto f = [&] { c++; };

  // IO exe doesn't support priorities
  IOThreadPoolExecutor ioExe(10);
  EXPECT_THROW(ioExe.addWithPriority(f, 0), std::runtime_error);

  CPUThreadPoolExecutor cpuExe(10, 3);
  cpuExe.addWithPriority(f, -1);
  cpuExe.addWithPriority(f, 0);
  cpuExe.addWithPriority(f, 1);
  cpuExe.addWithPriority(f, -2); // will add at the lowest priority
  cpuExe.addWithPriority(f, 2); // will add at the highest priority
  cpuExe.addWithPriority(f, Executor::LO_PRI);
  cpuExe.addWithPriority(f, Executor::HI_PRI);
  cpuExe.join();

  EXPECT_EQ(7, c);
}

TEST(ThreadPoolExecutorTest, BlockingQueue) {
  std::atomic_int c{0};
  auto f = [&] {
    burnMs(1)();
    c++;
  };
  const int kQueueCapacity = 1;
  const int kThreads = 1;

  auto queue = std::make_unique<LifoSemMPMCQueue<
      CPUThreadPoolExecutor::CPUTask,
      QueueBehaviorIfFull::BLOCK>>(kQueueCapacity);

  CPUThreadPoolExecutor cpuExe(
      kThreads,
      std::move(queue),
      std::make_shared<NamedThreadFactory>("CPUThreadPool"));

  // Add `f` five times. It sleeps for 1ms every time. Calling
  // `cppExec.add()` is *almost* guaranteed to block because there's
  // only 1 cpu worker thread.
  for (int i = 0; i < 5; i++) {
    EXPECT_NO_THROW(cpuExe.add(f));
  }
  cpuExe.join();

  EXPECT_EQ(5, c);
}

TEST(PriorityThreadFactoryTest, ThreadPriority) {
  errno = 0;
  auto currentPriority = getpriority(PRIO_PROCESS, 0);
  if (errno != 0) {
    throwSystemError("failed to get current priority");
  }

  // Non-root users can only increase the priority value.  Make sure we are
  // trying to go to a higher priority than we are currently running as, up to
  // the maximum allowed of 20.
  int desiredPriority = std::min(20, currentPriority + 1);

  PriorityThreadFactory factory(
      std::make_shared<NamedThreadFactory>("stuff"), desiredPriority);
  int actualPriority = -21;
  factory.newThread([&]() { actualPriority = getpriority(PRIO_PROCESS, 0); })
      .join();
  EXPECT_EQ(desiredPriority, actualPriority);
}

class TestData : public folly::RequestData {
 public:
  explicit TestData(int data) : data_(data) {}
  ~TestData() override {}

  bool hasCallback() override {
    return false;
  }

  int data_;
};

TEST(ThreadPoolExecutorTest, RequestContext) {
  CPUThreadPoolExecutor executor(1);

  RequestContextScopeGuard rctx; // create new request context for this scope
  EXPECT_EQ(nullptr, RequestContext::get()->getContextData("test"));
  RequestContext::get()->setContextData("test", std::make_unique<TestData>(42));
  auto data = RequestContext::get()->getContextData("test");
  EXPECT_EQ(42, dynamic_cast<TestData*>(data)->data_);

  executor.add([] {
    auto data = RequestContext::get()->getContextData("test");
    ASSERT_TRUE(data != nullptr);
    EXPECT_EQ(42, dynamic_cast<TestData*>(data)->data_);
  });
}

struct SlowMover {
  explicit SlowMover(bool slow = false) : slow(slow) {}
  SlowMover(SlowMover&& other) noexcept {
    *this = std::move(other);
  }
  SlowMover& operator=(SlowMover&& other) noexcept {
    slow = other.slow;
    if (slow) {
      /* sleep override */ std::this_thread::sleep_for(milliseconds(50));
    }
    return *this;
  }

  bool slow;
};

template <typename Q>
void bugD3527722_test() {
  // Test that the queue does not get stuck if writes are completed in
  // order opposite to how they are initiated.
  Q q(1024);
  std::atomic<int> turn{};

  std::thread consumer1([&] {
    ++turn;
    q.take();
  });
  std::thread consumer2([&] {
    ++turn;
    q.take();
  });

  std::thread producer1([&] {
    ++turn;
    while (turn < 4) {
      ;
    }
    ++turn;
    q.add(SlowMover(true));
  });
  std::thread producer2([&] {
    ++turn;
    while (turn < 5) {
      ;
    }
    q.add(SlowMover(false));
  });

  producer1.join();
  producer2.join();
  consumer1.join();
  consumer2.join();
}

TEST(ThreadPoolExecutorTest, LifoSemMPMCQueueBugD3527722) {
  bugD3527722_test<LifoSemMPMCQueue<SlowMover>>();
}

template <typename T>
struct UBQ : public UnboundedBlockingQueue<T> {
  explicit UBQ(int) {}
};

TEST(ThreadPoolExecutorTest, UnboundedBlockingQueueBugD3527722) {
  bugD3527722_test<UBQ<SlowMover>>();
}

template <typename TPE, typename ERR_T>
static void ShutdownTest() {
  // test that adding a .then() after we have
  // started shutting down does not deadlock
  folly::Optional<folly::Future<int>> f;
  {
    TPE fe(1);
    f = folly::makeFuture().via(&fe).then([]() { burnMs(100)(); }).then([]() {
      return 77;
    });
  }
  EXPECT_THROW(f->get(), ERR_T);
}

TEST(ThreadPoolExecutorTest, ShutdownTestIO) {
  ShutdownTest<IOThreadPoolExecutor, std::runtime_error>();
}

TEST(ThreadPoolExecutorTest, ShutdownTestCPU) {
  ShutdownTest<CPUThreadPoolExecutor, folly::FutureException>();
}

template <typename TPE>
static void removeThreadTest() {
  // test that adding a .then() after we have removed some threads
  // doesn't cause deadlock and they are executed on different threads
  folly::Optional<folly::Future<int>> f;
  std::thread::id id1, id2;
  TPE fe(2);
  f = folly::makeFuture()
          .via(&fe)
          .then([&id1]() {
            burnMs(100)();
            id1 = std::this_thread::get_id();
          })
          .then([&id2]() {
            return 77;
            id2 = std::this_thread::get_id();
          });
  fe.setNumThreads(1);

  // future::then should be fulfilled because there is other thread available
  EXPECT_EQ(77, f->get());
  // two thread should be different because then part should be rescheduled to
  // the other thread
  EXPECT_NE(id1, id2);
}

TEST(ThreadPoolExecutorTest, RemoveThreadTestIO) {
  removeThreadTest<IOThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, RemoveThreadTestCPU) {
  removeThreadTest<CPUThreadPoolExecutor>();
}

template <typename TPE>
static void resizeThreadWhileExecutingTest() {
  TPE tpe(10);
  EXPECT_EQ(10, tpe.numThreads());

  std::atomic<int> completed(0);
  auto f = [&]() {
    burnMs(10)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.setNumThreads(8);
  EXPECT_EQ(8, tpe.numThreads());
  tpe.setNumThreads(5);
  EXPECT_EQ(5, tpe.numThreads());
  tpe.setNumThreads(15);
  EXPECT_EQ(15, tpe.numThreads());
  tpe.stop();
  EXPECT_EQ(1000, completed);
}

TEST(ThreadPoolExecutorTest, resizeThreadWhileExecutingTestIO) {
  resizeThreadWhileExecutingTest<IOThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, resizeThreadWhileExecutingTestCPU) {
  resizeThreadWhileExecutingTest<CPUThreadPoolExecutor>();
}
