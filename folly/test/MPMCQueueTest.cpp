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

#include <folly/MPMCQueue.h>
#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/test/DeterministicSchedule.h>

#include <boost/intrusive_ptr.hpp>
#include <memory>
#include <functional>
#include <thread>
#include <utility>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

FOLLY_ASSUME_FBVECTOR_COMPATIBLE_1(boost::intrusive_ptr);

using namespace folly;
using namespace detail;
using namespace test;

typedef DeterministicSchedule DSched;

template <template<typename> class Atom>
void run_mt_sequencer_thread(
    int numThreads,
    int numOps,
    uint32_t init,
    TurnSequencer<Atom>& seq,
    Atom<uint32_t>& spinThreshold,
    int& prev,
    int i) {
  for (int op = i; op < numOps; op += numThreads) {
    seq.waitForTurn(init + op, spinThreshold, (op % 32) == 0);
    EXPECT_EQ(prev, op - 1);
    prev = op;
    seq.completeTurn(init + op);
  }
}

template <template<typename> class Atom>
void run_mt_sequencer_test(int numThreads, int numOps, uint32_t init) {
  TurnSequencer<Atom> seq(init);
  Atom<uint32_t> spinThreshold(0);

  int prev = -1;
  std::vector<std::thread> threads(numThreads);
  for (int i = 0; i < numThreads; ++i) {
    threads[i] = DSched::thread(std::bind(run_mt_sequencer_thread<Atom>,
          numThreads, numOps, init, std::ref(seq), std::ref(spinThreshold),
          std::ref(prev), i));
  }

  for (auto& thr : threads) {
    DSched::join(thr);
  }

  EXPECT_EQ(prev, numOps - 1);
}

TEST(MPMCQueue, sequencer) {
  run_mt_sequencer_test<std::atomic>(1, 100, 0);
  run_mt_sequencer_test<std::atomic>(2, 100000, -100);
  run_mt_sequencer_test<std::atomic>(100, 10000, -100);
}

TEST(MPMCQueue, sequencer_emulated_futex) {
  run_mt_sequencer_test<EmulatedFutexAtomic>(1, 100, 0);
  run_mt_sequencer_test<EmulatedFutexAtomic>(2, 100000, -100);
  run_mt_sequencer_test<EmulatedFutexAtomic>(100, 10000, -100);
}

TEST(MPMCQueue, sequencer_deterministic) {
  DSched sched(DSched::uniform(0));
  run_mt_sequencer_test<DeterministicAtomic>(1, 100, -50);
  run_mt_sequencer_test<DeterministicAtomic>(2, 10000, (1 << 29) - 100);
  run_mt_sequencer_test<DeterministicAtomic>(10, 1000, -100);
}

template <typename T>
void runElementTypeTest(T&& src) {
  MPMCQueue<T> cq(10);
  cq.blockingWrite(std::move(src));
  T dest;
  cq.blockingRead(dest);
  EXPECT_TRUE(cq.write(std::move(dest)));
  EXPECT_TRUE(cq.read(dest));
}

struct RefCounted {
  static __thread int active_instances;

  mutable std::atomic<int> rc;

  RefCounted() : rc(0) {
    ++active_instances;
  }

  ~RefCounted() {
    --active_instances;
  }
};
__thread int RefCounted::active_instances;


void intrusive_ptr_add_ref(RefCounted const* p) {
  p->rc++;
}

void intrusive_ptr_release(RefCounted const* p) {
  if (--(p->rc) == 0) {
    delete p;
  }
}

TEST(MPMCQueue, lots_of_element_types) {
  runElementTypeTest(10);
  runElementTypeTest(std::string("abc"));
  runElementTypeTest(std::make_pair(10, std::string("def")));
  runElementTypeTest(std::vector<std::string>{ { "abc" } });
  runElementTypeTest(std::make_shared<char>('a'));
  runElementTypeTest(folly::make_unique<char>('a'));
  runElementTypeTest(boost::intrusive_ptr<RefCounted>(new RefCounted));
  EXPECT_EQ(RefCounted::active_instances, 0);
}

TEST(MPMCQueue, single_thread_enqdeq) {
  MPMCQueue<int> cq(10);

  for (int pass = 0; pass < 10; ++pass) {
    for (int i = 0; i < 10; ++i) {
      EXPECT_TRUE(cq.write(i));
    }
    EXPECT_FALSE(cq.write(-1));
    EXPECT_FALSE(cq.isEmpty());
    EXPECT_EQ(cq.size(), 10);

    for (int i = 0; i < 5; ++i) {
      int dest = -1;
      EXPECT_TRUE(cq.read(dest));
      EXPECT_EQ(dest, i);
    }
    for (int i = 5; i < 10; ++i) {
      int dest = -1;
      cq.blockingRead(dest);
      EXPECT_EQ(dest, i);
    }
    int dest = -1;
    EXPECT_FALSE(cq.read(dest));
    EXPECT_EQ(dest, -1);

    EXPECT_TRUE(cq.isEmpty());
    EXPECT_EQ(cq.size(), 0);
  }
}

TEST(MPMCQueue, tryenq_capacity_test) {
  for (size_t cap = 1; cap < 100; ++cap) {
    MPMCQueue<int> cq(cap);
    for (size_t i = 0; i < cap; ++i) {
      EXPECT_TRUE(cq.write(i));
    }
    EXPECT_FALSE(cq.write(100));
  }
}

TEST(MPMCQueue, enq_capacity_test) {
  for (auto cap : { 1, 100, 10000 }) {
    MPMCQueue<int> cq(cap);
    for (int i = 0; i < cap; ++i) {
      cq.blockingWrite(i);
    }
    int t = 0;
    int when;
    auto thr = std::thread([&]{
      cq.blockingWrite(100);
      when = t;
    });
    usleep(2000);
    t = 1;
    int dummy;
    cq.blockingRead(dummy);
    thr.join();
    EXPECT_EQ(when, 1);
  }
}

template <template<typename> class Atom>
void runTryEnqDeqThread(
    int numThreads,
    int n, /*numOps*/
    MPMCQueue<int, Atom>& cq,
    std::atomic<uint64_t>& sum,
    int t) {
  uint64_t threadSum = 0;
  int src = t;
  // received doesn't reflect any actual values, we just start with
  // t and increment by numThreads to get the rounding of termination
  // correct if numThreads doesn't evenly divide numOps
  int received = t;
  while (src < n || received < n) {
    if (src < n && cq.write(src)) {
      src += numThreads;
    }

    int dst;
    if (received < n && cq.read(dst)) {
      received += numThreads;
      threadSum += dst;
    }
  }
  sum += threadSum;
}

template <template<typename> class Atom>
void runTryEnqDeqTest(int numThreads, int numOps) {
  // write and read aren't linearizable, so we don't have
  // hard guarantees on their individual behavior.  We can still test
  // correctness in aggregate
  MPMCQueue<int,Atom> cq(numThreads);

  uint64_t n = numOps;
  std::vector<std::thread> threads(numThreads);
  std::atomic<uint64_t> sum(0);
  for (int t = 0; t < numThreads; ++t) {
    threads[t] = DSched::thread(std::bind(runTryEnqDeqThread<Atom>,
          numThreads, n, std::ref(cq), std::ref(sum), t));
  }
  for (auto& t : threads) {
    DSched::join(t);
  }
  EXPECT_TRUE(cq.isEmpty());
  EXPECT_EQ(n * (n - 1) / 2 - sum, 0);
}

TEST(MPMCQueue, mt_try_enq_deq) {
  int nts[] = { 1, 3, 100 };

  int n = 100000;
  for (int nt : nts) {
    runTryEnqDeqTest<std::atomic>(nt, n);
  }
}

TEST(MPMCQueue, mt_try_enq_deq_emulated_futex) {
  int nts[] = { 1, 3, 100 };

  int n = 100000;
  for (int nt : nts) {
    runTryEnqDeqTest<EmulatedFutexAtomic>(nt, n);
  }
}

TEST(MPMCQueue, mt_try_enq_deq_deterministic) {
  int nts[] = { 3, 10 };

  long seed = 0;
  LOG(INFO) << "using seed " << seed;

  int n = 1000;
  for (int nt : nts) {
    {
      DSched sched(DSched::uniform(seed));
      runTryEnqDeqTest<DeterministicAtomic>(nt, n);
    }
    {
      DSched sched(DSched::uniformSubset(seed, 2));
      runTryEnqDeqTest<DeterministicAtomic>(nt, n);
    }
  }
}

uint64_t nowMicro() {
  timeval tv;
  gettimeofday(&tv, 0);
  return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

template <typename Q>
std::string producerConsumerBench(Q&& queue, std::string qName,
                                  int numProducers, int numConsumers,
                                  int numOps, bool ignoreContents = false) {
  Q& q = queue;

  struct rusage beginUsage;
  getrusage(RUSAGE_SELF, &beginUsage);

  auto beginMicro = nowMicro();

  uint64_t n = numOps;
  std::atomic<uint64_t> sum(0);

  std::vector<std::thread> producers(numProducers);
  for (int t = 0; t < numProducers; ++t) {
    producers[t] = DSched::thread([&,t]{
      for (int i = t; i < numOps; i += numProducers) {
        q.blockingWrite(i);
      }
    });
  }

  std::vector<std::thread> consumers(numConsumers);
  for (int t = 0; t < numConsumers; ++t) {
    consumers[t] = DSched::thread([&,t]{
      uint64_t localSum = 0;
      for (int i = t; i < numOps; i += numConsumers) {
        int dest = -1;
        q.blockingRead(dest);
        EXPECT_FALSE(dest == -1);
        localSum += dest;
      }
      sum += localSum;
    });
  }

  for (auto& t : producers) {
    DSched::join(t);
  }
  for (auto& t : consumers) {
    DSched::join(t);
  }
  if (!ignoreContents) {
    EXPECT_EQ(n * (n - 1) / 2 - sum, 0);
  }

  auto endMicro = nowMicro();

  struct rusage endUsage;
  getrusage(RUSAGE_SELF, &endUsage);

  uint64_t nanosPer = (1000 * (endMicro - beginMicro)) / n;
  long csw = endUsage.ru_nvcsw + endUsage.ru_nivcsw -
      (beginUsage.ru_nvcsw + beginUsage.ru_nivcsw);

  return folly::format(
      "{}, {} producers, {} consumers => {} nanos/handoff, {} csw / {} handoff",
      qName, numProducers, numConsumers, nanosPer, csw, n).str();
}


TEST(MPMCQueue, mt_prod_cons_deterministic) {
  // we use the Bench method, but perf results are meaningless under DSched
  DSched sched(DSched::uniform(0));

  producerConsumerBench(MPMCQueue<int,DeterministicAtomic>(10),
          "", 1, 1, 1000);
  producerConsumerBench(MPMCQueue<int,DeterministicAtomic>(100),
          "", 10, 10, 1000);
  producerConsumerBench(MPMCQueue<int,DeterministicAtomic>(10),
          "", 1, 1, 1000);
  producerConsumerBench(MPMCQueue<int,DeterministicAtomic>(100),
          "", 10, 10, 1000);
  producerConsumerBench(MPMCQueue<int,DeterministicAtomic>(1),
          "", 10, 10, 1000);
}

#define PC_BENCH(q, np, nc, ...) \
    producerConsumerBench(q, #q, (np), (nc), __VA_ARGS__)

TEST(MPMCQueue, mt_prod_cons) {
  int n = 100000;
  LOG(INFO) << PC_BENCH(MPMCQueue<int>(10), 1, 1, n);
  LOG(INFO) << PC_BENCH(MPMCQueue<int>(10), 10, 1, n);
  LOG(INFO) << PC_BENCH(MPMCQueue<int>(10), 1, 10, n);
  LOG(INFO) << PC_BENCH(MPMCQueue<int>(10), 10, 10, n);
  LOG(INFO) << PC_BENCH(MPMCQueue<int>(10000), 1, 1, n);
  LOG(INFO) << PC_BENCH(MPMCQueue<int>(10000), 10, 1, n);
  LOG(INFO) << PC_BENCH(MPMCQueue<int>(10000), 1, 10, n);
  LOG(INFO) << PC_BENCH(MPMCQueue<int>(10000), 10, 10, n);
  LOG(INFO) << PC_BENCH(MPMCQueue<int>(100000), 32, 100, n);
}

TEST(MPMCQueue, mt_prod_cons_emulated_futex) {
  int n = 100000;
  LOG(INFO) << PC_BENCH((MPMCQueue<int,EmulatedFutexAtomic>(10)), 1, 1, n);
  LOG(INFO) << PC_BENCH((MPMCQueue<int,EmulatedFutexAtomic>(10)), 10, 1, n);
  LOG(INFO) << PC_BENCH((MPMCQueue<int,EmulatedFutexAtomic>(10)), 1, 10, n);
  LOG(INFO) << PC_BENCH((MPMCQueue<int,EmulatedFutexAtomic>(10)), 10, 10, n);
  LOG(INFO) << PC_BENCH((MPMCQueue<int,EmulatedFutexAtomic>(10000)), 1, 1, n);
  LOG(INFO) << PC_BENCH((MPMCQueue<int,EmulatedFutexAtomic>(10000)), 10, 1, n);
  LOG(INFO) << PC_BENCH((MPMCQueue<int,EmulatedFutexAtomic>(10000)), 1, 10, n);
  LOG(INFO) << PC_BENCH((MPMCQueue<int,EmulatedFutexAtomic>(10000)), 10, 10, n);
  LOG(INFO)
    << PC_BENCH((MPMCQueue<int,EmulatedFutexAtomic>(100000)), 32, 100, n);
}

template <template<typename> class Atom>
void runNeverFailThread(
    int numThreads,
    int n, /*numOps*/
    MPMCQueue<int, Atom>& cq,
    std::atomic<uint64_t>& sum,
    int t) {
  uint64_t threadSum = 0;
  for (int i = t; i < n; i += numThreads) {
    // enq + deq
    EXPECT_TRUE(cq.writeIfNotFull(i));

    int dest = -1;
    EXPECT_TRUE(cq.readIfNotEmpty(dest));
    EXPECT_TRUE(dest >= 0);
    threadSum += dest;
  }
  sum += threadSum;
}

template <template<typename> class Atom>
uint64_t runNeverFailTest(int numThreads, int numOps) {
  // always #enq >= #deq
  MPMCQueue<int,Atom> cq(numThreads);

  uint64_t n = numOps;
  auto beginMicro = nowMicro();

  std::vector<std::thread> threads(numThreads);
  std::atomic<uint64_t> sum(0);
  for (int t = 0; t < numThreads; ++t) {
    threads[t] = DSched::thread(std::bind(runNeverFailThread<Atom>,
          numThreads, n, std::ref(cq), std::ref(sum), t));
  }
  for (auto& t : threads) {
    DSched::join(t);
  }
  EXPECT_TRUE(cq.isEmpty());
  EXPECT_EQ(n * (n - 1) / 2 - sum, 0);

  return nowMicro() - beginMicro;
}

TEST(MPMCQueue, mt_never_fail) {
  int nts[] = { 1, 3, 100 };

  int n = 100000;
  for (int nt : nts) {
    uint64_t elapsed = runNeverFailTest<std::atomic>(nt, n);
    LOG(INFO) << (elapsed * 1000.0) / (n * 2) << " nanos per op with "
              << nt << " threads";
  }
}

TEST(MPMCQueue, mt_never_fail_emulated_futex) {
  int nts[] = { 1, 3, 100 };

  int n = 100000;
  for (int nt : nts) {
    uint64_t elapsed = runNeverFailTest<EmulatedFutexAtomic>(nt, n);
    LOG(INFO) << (elapsed * 1000.0) / (n * 2) << " nanos per op with "
              << nt << " threads";
  }
}

TEST(MPMCQueue, mt_never_fail_deterministic) {
  int nts[] = { 3, 10 };

  long seed = 0; // nowMicro() % 10000;
  LOG(INFO) << "using seed " << seed;

  int n = 1000;
  for (int nt : nts) {
    {
      DSched sched(DSched::uniform(seed));
      runNeverFailTest<DeterministicAtomic>(nt, n);
    }
    {
      DSched sched(DSched::uniformSubset(seed, 2));
      runNeverFailTest<DeterministicAtomic>(nt, n);
    }
  }
}

enum LifecycleEvent {
  NOTHING = -1,
  DEFAULT_CONSTRUCTOR,
  COPY_CONSTRUCTOR,
  MOVE_CONSTRUCTOR,
  TWO_ARG_CONSTRUCTOR,
  COPY_OPERATOR,
  MOVE_OPERATOR,
  DESTRUCTOR,
  MAX_LIFECYCLE_EVENT
};

static FOLLY_TLS int lc_counts[MAX_LIFECYCLE_EVENT];
static FOLLY_TLS int lc_prev[MAX_LIFECYCLE_EVENT];

static int lc_outstanding() {
  return lc_counts[DEFAULT_CONSTRUCTOR] + lc_counts[COPY_CONSTRUCTOR] +
      lc_counts[MOVE_CONSTRUCTOR] + lc_counts[TWO_ARG_CONSTRUCTOR] -
      lc_counts[DESTRUCTOR];
}

static void lc_snap() {
  for (int i = 0; i < MAX_LIFECYCLE_EVENT; ++i) {
    lc_prev[i] = lc_counts[i];
  }
}

#define LIFECYCLE_STEP(...) lc_step(__LINE__, __VA_ARGS__)

static void lc_step(int lineno, int what = NOTHING, int what2 = NOTHING) {
  for (int i = 0; i < MAX_LIFECYCLE_EVENT; ++i) {
    int delta = i == what || i == what2 ? 1 : 0;
    EXPECT_EQ(lc_counts[i] - lc_prev[i], delta)
        << "lc_counts[" << i << "] - lc_prev[" << i << "] was "
        << (lc_counts[i] - lc_prev[i]) << ", expected " << delta
        << ", from line " << lineno;
  }
  lc_snap();
}

template <typename R>
struct Lifecycle {
  typedef R IsRelocatable;

  bool constructed;

  Lifecycle() noexcept : constructed(true) {
    ++lc_counts[DEFAULT_CONSTRUCTOR];
  }

  explicit Lifecycle(int n, char const* s) noexcept : constructed(true) {
    ++lc_counts[TWO_ARG_CONSTRUCTOR];
  }

  Lifecycle(const Lifecycle& rhs) noexcept : constructed(true) {
    ++lc_counts[COPY_CONSTRUCTOR];
  }

  Lifecycle(Lifecycle&& rhs) noexcept : constructed(true) {
    ++lc_counts[MOVE_CONSTRUCTOR];
  }

  Lifecycle& operator= (const Lifecycle& rhs) noexcept {
    ++lc_counts[COPY_OPERATOR];
    return *this;
  }

  Lifecycle& operator= (Lifecycle&& rhs) noexcept {
    ++lc_counts[MOVE_OPERATOR];
    return *this;
  }

  ~Lifecycle() noexcept {
    ++lc_counts[DESTRUCTOR];
    assert(lc_outstanding() >= 0);
    assert(constructed);
    constructed = false;
  }
};

template <typename R>
void runPerfectForwardingTest() {
  lc_snap();
  EXPECT_EQ(lc_outstanding(), 0);

  {
    MPMCQueue<Lifecycle<R>> queue(50);
    LIFECYCLE_STEP(NOTHING);

    for (int pass = 0; pass < 10; ++pass) {
      for (int i = 0; i < 10; ++i) {
        queue.blockingWrite();
        LIFECYCLE_STEP(DEFAULT_CONSTRUCTOR);

        queue.blockingWrite(1, "one");
        LIFECYCLE_STEP(TWO_ARG_CONSTRUCTOR);

        {
          Lifecycle<R> src;
          LIFECYCLE_STEP(DEFAULT_CONSTRUCTOR);
          queue.blockingWrite(std::move(src));
          LIFECYCLE_STEP(MOVE_CONSTRUCTOR);
        }
        LIFECYCLE_STEP(DESTRUCTOR);

        {
          Lifecycle<R> src;
          LIFECYCLE_STEP(DEFAULT_CONSTRUCTOR);
          queue.blockingWrite(src);
          LIFECYCLE_STEP(COPY_CONSTRUCTOR);
        }
        LIFECYCLE_STEP(DESTRUCTOR);

        EXPECT_TRUE(queue.write());
        LIFECYCLE_STEP(DEFAULT_CONSTRUCTOR);
      }

      EXPECT_EQ(queue.size(), 50);
      EXPECT_FALSE(queue.write(2, "two"));
      LIFECYCLE_STEP(NOTHING);

      for (int i = 0; i < 50; ++i) {
        {
          Lifecycle<R> node;
          LIFECYCLE_STEP(DEFAULT_CONSTRUCTOR);

          queue.blockingRead(node);
          if (R::value) {
            // relocatable, moved via memcpy
            LIFECYCLE_STEP(DESTRUCTOR);
          } else {
            LIFECYCLE_STEP(DESTRUCTOR, MOVE_OPERATOR);
          }
        }
        LIFECYCLE_STEP(DESTRUCTOR);
      }

      EXPECT_EQ(queue.size(), 0);
    }

    // put one element back before destruction
    {
      Lifecycle<R> src(3, "three");
      LIFECYCLE_STEP(TWO_ARG_CONSTRUCTOR);
      queue.write(std::move(src));
      LIFECYCLE_STEP(MOVE_CONSTRUCTOR);
    }
    LIFECYCLE_STEP(DESTRUCTOR); // destroy src
  }
  LIFECYCLE_STEP(DESTRUCTOR); // destroy queue

  EXPECT_EQ(lc_outstanding(), 0);
}

TEST(MPMCQueue, perfect_forwarding) {
  runPerfectForwardingTest<std::false_type>();
}

TEST(MPMCQueue, perfect_forwarding_relocatable) {
  runPerfectForwardingTest<std::true_type>();
}

TEST(MPMCQueue, queue_moving) {
  lc_snap();
  EXPECT_EQ(lc_outstanding(), 0);

  {
    MPMCQueue<Lifecycle<std::false_type>> a(50);
    LIFECYCLE_STEP(NOTHING);

    a.blockingWrite();
    LIFECYCLE_STEP(DEFAULT_CONSTRUCTOR);

    // move constructor
    MPMCQueue<Lifecycle<std::false_type>> b = std::move(a);
    LIFECYCLE_STEP(NOTHING);
    EXPECT_EQ(a.capacity(), 0);
    EXPECT_EQ(a.size(), 0);
    EXPECT_EQ(b.capacity(), 50);
    EXPECT_EQ(b.size(), 1);

    b.blockingWrite();
    LIFECYCLE_STEP(DEFAULT_CONSTRUCTOR);

    // move operator
    MPMCQueue<Lifecycle<std::false_type>> c;
    LIFECYCLE_STEP(NOTHING);
    c = std::move(b);
    LIFECYCLE_STEP(NOTHING);
    EXPECT_EQ(c.capacity(), 50);
    EXPECT_EQ(c.size(), 2);

    {
      Lifecycle<std::false_type> dst;
      LIFECYCLE_STEP(DEFAULT_CONSTRUCTOR);
      c.blockingRead(dst);
      LIFECYCLE_STEP(DESTRUCTOR, MOVE_OPERATOR);

      {
        // swap
        MPMCQueue<Lifecycle<std::false_type>> d(10);
        LIFECYCLE_STEP(NOTHING);
        std::swap(c, d);
        LIFECYCLE_STEP(NOTHING);
        EXPECT_EQ(c.capacity(), 10);
        EXPECT_TRUE(c.isEmpty());
        EXPECT_EQ(d.capacity(), 50);
        EXPECT_EQ(d.size(), 1);

        d.blockingRead(dst);
        LIFECYCLE_STEP(DESTRUCTOR, MOVE_OPERATOR);

        c.blockingWrite(dst);
        LIFECYCLE_STEP(COPY_CONSTRUCTOR);

        d.blockingWrite(std::move(dst));
        LIFECYCLE_STEP(MOVE_CONSTRUCTOR);
      } // d goes out of scope
      LIFECYCLE_STEP(DESTRUCTOR);
    } // dst goes out of scope
    LIFECYCLE_STEP(DESTRUCTOR);
  } // c goes out of scope
  LIFECYCLE_STEP(DESTRUCTOR);
}

int main(int argc, char ** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
