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

#include <folly/Benchmark.h>
#include <folly/wangle/rx/Observer.h>
#include <folly/wangle/rx/Subject.h>
#include <gflags/gflags.h>

using namespace folly::wangle;
using folly::BenchmarkSuspender;

static std::unique_ptr<Observer<int>> makeObserver() {
  return Observer<int>::create([&] (int x) {});
}

void subscribeImpl(uint iters, int N, bool countUnsubscribe) {
  for (uint iter = 0; iter < iters; iter++) {
    BenchmarkSuspender bs;
    Subject<int> subject;
    std::vector<std::unique_ptr<Observer<int>>> observers;
    std::vector<Subscription<int>> subscriptions;
    subscriptions.reserve(N);
    for (int i = 0; i < N; i++) {
      observers.push_back(makeObserver());
    }
    bs.dismiss();
    for (int i = 0; i < N; i++) {
      subscriptions.push_back(subject.subscribe(std::move(observers[i])));
    }
    if (countUnsubscribe) {
      subscriptions.clear();
    }
    bs.rehire();
  }
}

void subscribeAndUnsubscribe(uint iters, int N) {
  subscribeImpl(iters, N, true);
}

void subscribe(uint iters, int N) {
  subscribeImpl(iters, N, false);
}

void observe(uint iters, int N) {
  for (uint iter = 0; iter < iters; iter++) {
    BenchmarkSuspender bs;
    Subject<int> subject;
    std::vector<std::unique_ptr<Observer<int>>> observers;
    for (int i = 0; i < N; i++) {
      observers.push_back(makeObserver());
    }
    bs.dismiss();
    for (int i = 0; i < N; i++) {
      subject.observe(std::move(observers[i]));
    }
    bs.rehire();
  }
}

void inlineObserve(uint iters, int N) {
  for (uint iter = 0; iter < iters; iter++) {
    BenchmarkSuspender bs;
    Subject<int> subject;
    std::vector<Observer<int>*> observers;
    for (int i = 0; i < N; i++) {
      observers.push_back(makeObserver().release());
    }
    bs.dismiss();
    for (int i = 0; i < N; i++) {
      subject.observe(observers[i]);
    }
    bs.rehire();
    for (int i = 0; i < N; i++) {
      delete observers[i];
    }
  }
}

void notifySubscribers(uint iters, int N) {
  for (uint iter = 0; iter < iters; iter++) {
    BenchmarkSuspender bs;
    Subject<int> subject;
    std::vector<std::unique_ptr<Observer<int>>> observers;
    std::vector<Subscription<int>> subscriptions;
    subscriptions.reserve(N);
    for (int i = 0; i < N; i++) {
      observers.push_back(makeObserver());
    }
    for (int i = 0; i < N; i++) {
      subscriptions.push_back(subject.subscribe(std::move(observers[i])));
    }
    bs.dismiss();
    subject.onNext(42);
    bs.rehire();
  }
}

void notifyInlineObservers(uint iters, int N) {
  for (uint iter = 0; iter < iters; iter++) {
    BenchmarkSuspender bs;
    Subject<int> subject;
    std::vector<Observer<int>*> observers;
    for (int i = 0; i < N; i++) {
      observers.push_back(makeObserver().release());
    }
    for (int i = 0; i < N; i++) {
      subject.observe(observers[i]);
    }
    bs.dismiss();
    subject.onNext(42);
    bs.rehire();
  }
}

BENCHMARK_PARAM(subscribeAndUnsubscribe, 1);
BENCHMARK_RELATIVE_PARAM(subscribe, 1);
BENCHMARK_RELATIVE_PARAM(observe, 1);
BENCHMARK_RELATIVE_PARAM(inlineObserve, 1);

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(subscribeAndUnsubscribe, 1000);
BENCHMARK_RELATIVE_PARAM(subscribe, 1000);
BENCHMARK_RELATIVE_PARAM(observe, 1000);
BENCHMARK_RELATIVE_PARAM(inlineObserve, 1000);

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(notifySubscribers, 1);
BENCHMARK_RELATIVE_PARAM(notifyInlineObservers, 1);

BENCHMARK_DRAW_LINE();

BENCHMARK_PARAM(notifySubscribers, 1000);
BENCHMARK_RELATIVE_PARAM(notifyInlineObservers, 1000);

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
