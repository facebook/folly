/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <algorithm>
#include <thread>

#include <folly/Benchmark.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/NotificationQueue.h>
#include <folly/synchronization/Baton.h>

using namespace folly;

static size_t constexpr kMaxRead = 20;
static size_t constexpr kProducerWarmup = 1000;
static size_t constexpr kBusyLoopSize = 0;

class MockConsumer : public NotificationQueue<Func>::Consumer {
 public:
  void messageAvailable(Func&& message) noexcept override {
    message();
  }
};

static void burn(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(i);
  }
}

void multiProducerMultiConsumer(
    int iters,
    size_t numProducers,
    size_t numConsumers) {
  BenchmarkSuspender susp;
  NotificationQueue<Func> queue;
  std::vector<std::unique_ptr<EventBase>> consumerEventBases;
  std::vector<std::thread> consumerThreads;
  // Initialize consumers
  for (size_t i = 0; i < numConsumers; ++i) {
    // construct base without time measurements
    consumerEventBases.emplace_back(std::make_unique<EventBase>(false));
    EventBase& base = *consumerEventBases.back();
    consumerThreads.emplace_back([&base, &queue]() mutable {
      base.setMaxReadAtOnce(kMaxRead);
      MockConsumer consumer;
      consumer.startConsuming(&base, &queue);
      base.loopForever();
    });
  }

  std::vector<std::thread> producerThreads;
  std::atomic<size_t> producersWarmedUp{0};
  // Needs to be less than 0 so that consumers don't reach 0 during warm up
  std::atomic<ssize_t> itemsToProcess{-1};
  std::atomic<bool> stop_producing{false};
  Baton warmUpBaton;
  Baton finishedBaton;
  // Initialize producers and warm up both producers and consumers
  // during warm up producers produce kProducerWarmup tasks each and consumers
  // try to consume as much as possible
  for (size_t i = 0; i < numProducers; ++i) {
    producerThreads.emplace_back(std::thread([numProducers,
                                              &warmUpBaton,
                                              &queue,
                                              &producersWarmedUp,
                                              &itemsToProcess,
                                              &stop_producing,
                                              &finishedBaton]() mutable {
      size_t num_produced{0};
      while (!stop_producing.load(std::memory_order_relaxed)) {
        burn(kBusyLoopSize);
        if (num_produced++ == kProducerWarmup &&
            numProducers ==
                producersWarmedUp.fetch_add(1, std::memory_order_relaxed) + 1) {
          warmUpBaton.post();
        }
        queue.putMessage([&itemsToProcess, &finishedBaton]() {
          burn(kBusyLoopSize);
          if (itemsToProcess.fetch_sub(1, std::memory_order_relaxed) == 0) {
            finishedBaton.post();
          }
        });
      }
    }));
  }
  warmUpBaton.wait();
  susp.dismiss();
  // This sets itemsToProcess to desired iterations. Consumers reduce it with
  // every task. One which reduces it to 0 notifies via finishedBaton.
  itemsToProcess.store(iters, std::memory_order_relaxed);
  finishedBaton.wait();
  susp.rehire();
  // Stop producers
  stop_producing.store(true, std::memory_order_relaxed);
  for (auto& producerThread : producerThreads) {
    producerThread.join();
  }
  // Stop consumers
  for (auto& consumerEventBase : consumerEventBases) {
    consumerEventBase->terminateLoopSoon();
  }
  for (auto& consumerThread : consumerThreads) {
    consumerThread.join();
  }
}

BENCHMARK(EnqueueBenchmark, n) {
  BenchmarkSuspender suspender;
  NotificationQueue<Func> queue;
  suspender.dismiss();
  for (unsigned int i = 0; i < n; ++i) {
    queue.putMessage(Func());
  }
  suspender.rehire();
}

BENCHMARK(DequeueBenchmark, n) {
  BenchmarkSuspender suspender;
  NotificationQueue<Func> queue;
  EventBase base;
  MockConsumer consumer;
  consumer.setMaxReadAtOnce(kMaxRead);
  consumer.startConsumingInternal(&base, &queue);
  for (unsigned int i = 0; i < n; ++i) {
    queue.putMessage([]() {});
  }
  suspender.dismiss();
  for (unsigned int i = 0; i <= (n / kMaxRead); ++i) {
    consumer.handlerReady(0);
  }
  suspender.rehire();
  if (queue.size() > 0) {
    throw std::logic_error("This should not happen batching might be broken");
  }
  consumer.stopConsuming();
}

BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _1p__1c, 1, 1)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _2p__1c, 2, 1)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _4p__1c, 4, 1)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _8p__1c, 8, 1)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 16p__1c, 16, 1)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 32p__1c, 32, 1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _1p__2c, 1, 2)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _2p__2c, 2, 2)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _4p__2c, 4, 2)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _8p__2c, 8, 2)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 16p__2c, 16, 2)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 32p__2c, 32, 2)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _1p__4c, 1, 4)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _2p__4c, 2, 4)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _4p__4c, 4, 4)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _8p__4c, 8, 4)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 16p__4c, 16, 4)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 32p__4c, 32, 4)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _1p__8c, 1, 8)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _2p__8c, 2, 8)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _4p__8c, 4, 8)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _8p__8c, 8, 8)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 16p__8c, 16, 8)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 32p__8c, 32, 8)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _1p_16c, 1, 16)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _2p_16c, 2, 16)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _4p_16c, 4, 16)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _8p_16c, 8, 16)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 16p_16c, 16, 16)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 32p_16c, 32, 16)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _1p_32c, 1, 32)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _2p_32c, 2, 32)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _4p_32c, 4, 32)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, _8p_32c, 8, 32)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 16p_32c, 16, 32)
BENCHMARK_NAMED_PARAM(multiProducerMultiConsumer, 32p_32c, 32, 32)
BENCHMARK_DRAW_LINE();

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();

  return 0;
}
