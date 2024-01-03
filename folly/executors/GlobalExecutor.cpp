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

#include <folly/executors/GlobalExecutor.h>

#include <memory>
#include <thread>

#include <folly/Function.h>
#include <folly/SharedMutex.h>
#include <folly/Singleton.h>
#include <folly/detail/AsyncTrace.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/IOExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/InlineExecutor.h>
#include <folly/system/HardwareConcurrency.h>

using namespace folly;

FOLLY_GFLAGS_DEFINE_uint32(
    folly_global_io_executor_threads,
    0,
    "Number of threads global IOThreadPoolExecutor will create");

FOLLY_GFLAGS_DEFINE_uint32(
    folly_global_cpu_executor_threads,
    0,
    "Number of threads global CPUThreadPoolExecutor will create");

FOLLY_GFLAGS_DEFINE_bool(
    folly_global_cpu_executor_use_throttled_lifo_sem,
    true,
    "Use ThrottledLifoSem in global CPUThreadPoolExecutor");

FOLLY_GFLAGS_DEFINE_uint32(
    folly_global_cpu_executor_wake_up_interval_us,
    0,
    "If --folly_global_cpu_executor_use_throttled_lifo_sem is true, use this "
    "wake-up interval (in microseconds) in ThrottledLifoSem");

namespace {

using ImmutableGlobalCPUExecutor = CPUThreadPoolExecutor;

class GlobalTag {};

// aka InlineExecutor
class DefaultCPUExecutor : public InlineLikeExecutor {
 public:
  FOLLY_NOINLINE void add(Func f) override { f(); }
};

Singleton<std::shared_ptr<DefaultCPUExecutor>> gDefaultGlobalCPUExecutor([] {
  return new std::shared_ptr<DefaultCPUExecutor>(new DefaultCPUExecutor{});
});

Singleton<std::shared_ptr<Executor>, GlobalTag> gImmutableGlobalCPUExecutor([] {
  size_t nthreads = FLAGS_folly_global_cpu_executor_threads;
  nthreads = nthreads ? nthreads : folly::hardware_concurrency();
  return new std::shared_ptr<Executor>(new ImmutableGlobalCPUExecutor(
      nthreads,
      FLAGS_folly_global_cpu_executor_use_throttled_lifo_sem
          ? CPUThreadPoolExecutor::makeThrottledLifoSemQueue(
                std::chrono::microseconds{
                    FLAGS_folly_global_cpu_executor_wake_up_interval_us})
          : CPUThreadPoolExecutor::makeDefaultQueue(),
      std::make_shared<NamedThreadFactory>("GlobalCPUThreadPool")));
});

Singleton<std::shared_ptr<IOExecutor>, GlobalTag> gImmutableGlobalIOExecutor(
    [] {
      size_t nthreads = FLAGS_folly_global_io_executor_threads;
      nthreads = nthreads ? nthreads : folly::hardware_concurrency();
      return new std::shared_ptr<IOExecutor>(new IOThreadPoolExecutor(
          nthreads,
          std::make_shared<NamedThreadFactory>("GlobalIOThreadPool")));
    });

template <class ExecutorBase>
std::shared_ptr<std::shared_ptr<ExecutorBase>> getImmutablePtrPtr();

template <class ExecutorBase>
std::shared_ptr<ExecutorBase> getImmutable() {
  if (auto executorPtrPtr = getImmutablePtrPtr<ExecutorBase>()) {
    return *executorPtrPtr;
  }
  return nullptr;
}

template <>
std::shared_ptr<std::shared_ptr<Executor>> getImmutablePtrPtr() {
  return gImmutableGlobalCPUExecutor.try_get();
}

template <>
std::shared_ptr<std::shared_ptr<IOExecutor>> getImmutablePtrPtr() {
  return gImmutableGlobalIOExecutor.try_get();
}

template <class ExecutorBase>
class GlobalExecutor {
 public:
  explicit GlobalExecutor(
      Function<std::shared_ptr<ExecutorBase>()> constructDefault)
      : getDefault_(std::move(constructDefault)) {}

  std::shared_ptr<ExecutorBase> get() {
    std::shared_lock guard(mutex_);
    if (auto executor = executor_.lock()) {
      return executor; // Fast path.
    }

    return getDefault_();
  }

  void set(std::weak_ptr<ExecutorBase> executor) {
    std::unique_lock guard(mutex_);
    executor_.swap(executor);
  }

  // Replace the constructDefault function to use the immutable singleton
  // rather than the default singleton
  void setFromImmutable() {
    std::unique_lock guard(mutex_);

    getDefault_ = [] { return getImmutable<ExecutorBase>(); };
    executor_ = std::weak_ptr<ExecutorBase>{};
  }

 private:
  SharedMutex mutex_;
  std::weak_ptr<ExecutorBase> executor_;
  Function<std::shared_ptr<ExecutorBase>()> getDefault_;
};

LeakySingleton<GlobalExecutor<Executor>> gGlobalCPUExecutor([] {
  return new GlobalExecutor<Executor>(
      // Default global CPU executor is an InlineExecutor.
      [] {
        if (auto executorPtrPtr = gDefaultGlobalCPUExecutor.try_get()) {
          return *executorPtrPtr;
        }
        return std::shared_ptr<DefaultCPUExecutor>{};
      });
});

LeakySingleton<GlobalExecutor<IOExecutor>> gGlobalIOExecutor([] {
  return new GlobalExecutor<IOExecutor>(
      // Default global IO executor is an IOThreadPoolExecutor.
      [] { return getImmutable<IOExecutor>(); });
});
} // namespace

namespace folly {

namespace detail {
std::shared_ptr<Executor> tryGetImmutableCPUPtr() {
  return getImmutable<Executor>();
}
} // namespace detail

Executor::KeepAlive<> getGlobalCPUExecutor() {
  auto executorPtrPtr = getImmutablePtrPtr<Executor>();
  if (!executorPtrPtr) {
    throw std::runtime_error("Requested global CPU executor during shutdown.");
  }
  async_tracing::logGetImmutableCPUExecutor(executorPtrPtr->get());
  return folly::getKeepAliveToken(executorPtrPtr->get());
}

GlobalCPUExecutorCounters getGlobalCPUExecutorCounters() {
  auto executorPtrPtr = getImmutablePtrPtr<Executor>();
  if (!executorPtrPtr) {
    throw std::runtime_error("Requested global CPU executor during shutdown.");
  }
  auto& executor = dynamic_cast<ImmutableGlobalCPUExecutor&>(**executorPtrPtr);
  GlobalCPUExecutorCounters counters;
  counters.numThreads = executor.numThreads();
  counters.numActiveThreads = executor.numActiveThreads();
  counters.numPendingTasks = executor.getTaskQueueSize();
  return counters;
}

Executor::KeepAlive<IOExecutor> getGlobalIOExecutor() {
  auto executorPtrPtr = getImmutablePtrPtr<IOExecutor>();
  if (!executorPtrPtr) {
    throw std::runtime_error("Requested global IO executor during shutdown.");
  }
  async_tracing::logGetImmutableIOExecutor(executorPtrPtr->get());
  return folly::getKeepAliveToken(executorPtrPtr->get());
}

std::shared_ptr<Executor> getUnsafeMutableGlobalCPUExecutor() {
  auto& singleton = gGlobalCPUExecutor.get();
  auto executor = singleton.get();
  async_tracing::logGetGlobalCPUExecutor(executor.get());
  return executor;
}

std::shared_ptr<Executor> getCPUExecutor() {
  return getUnsafeMutableGlobalCPUExecutor();
}

void setUnsafeMutableGlobalCPUExecutorToGlobalCPUExecutor() {
  async_tracing::logSetGlobalCPUExecutorToImmutable();
  gGlobalCPUExecutor.get().setFromImmutable();
}

void setCPUExecutorToGlobalCPUExecutor() {
  setUnsafeMutableGlobalCPUExecutorToGlobalCPUExecutor();
}

void setUnsafeMutableGlobalCPUExecutor(std::weak_ptr<Executor> executor) {
  async_tracing::logSetGlobalCPUExecutor(executor.lock().get());
  gGlobalCPUExecutor.get().set(std::move(executor));
}

void setCPUExecutor(std::weak_ptr<Executor> executor) {
  setUnsafeMutableGlobalCPUExecutor(std::move(executor));
}

std::shared_ptr<IOExecutor> getUnsafeMutableGlobalIOExecutor() {
  auto& singleton = gGlobalIOExecutor.get();
  auto executor = singleton.get();
  async_tracing::logGetGlobalIOExecutor(executor.get());
  return executor;
}

std::shared_ptr<IOExecutor> getIOExecutor() {
  return getUnsafeMutableGlobalIOExecutor();
}

void setUnsafeMutableGlobalIOExecutor(std::weak_ptr<IOExecutor> executor) {
  async_tracing::logSetGlobalIOExecutor(executor.lock().get());
  gGlobalIOExecutor.get().set(std::move(executor));
}

void setIOExecutor(std::weak_ptr<IOExecutor> executor) {
  setUnsafeMutableGlobalIOExecutor(std::move(executor));
}

EventBase* getUnsafeMutableGlobalEventBase() {
  auto executor = getUnsafeMutableGlobalIOExecutor();
  if (FOLLY_LIKELY(!!executor)) {
    return executor->getEventBase();
  }

  return nullptr;
}

EventBase* getEventBase() {
  return getUnsafeMutableGlobalEventBase();
}

} // namespace folly
