/*
 * Copyright 2017 Facebook, Inc.
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

#include <folly/Singleton.h>
#include <folly/executors/IOExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/futures/InlineExecutor.h>

using namespace folly;

namespace {

// lock protecting global CPU executor
struct CPUExecutorLock {};
Singleton<RWSpinLock, CPUExecutorLock> globalCPUExecutorLock;
// global CPU executor
Singleton<std::weak_ptr<Executor>> globalCPUExecutor;
// default global CPU executor is an InlineExecutor
Singleton<std::shared_ptr<InlineExecutor>> globalInlineExecutor([] {
  return new std::shared_ptr<InlineExecutor>(
      std::make_shared<InlineExecutor>());
});

// lock protecting global IO executor
struct IOExecutorLock {};
Singleton<RWSpinLock, IOExecutorLock> globalIOExecutorLock;
// global IO executor
Singleton<std::weak_ptr<IOExecutor>> globalIOExecutor;
// default global IO executor is an IOThreadPoolExecutor
Singleton<std::shared_ptr<IOThreadPoolExecutor>> globalIOThreadPool([] {
  return new std::shared_ptr<IOThreadPoolExecutor>(
      std::make_shared<IOThreadPoolExecutor>(
          sysconf(_SC_NPROCESSORS_ONLN),
          std::make_shared<NamedThreadFactory>("GlobalIOThreadPool")));
});
}

namespace folly {

template <class Exe, class DefaultExe, class LockTag>
std::shared_ptr<Exe> getExecutor(
    Singleton<std::weak_ptr<Exe>>& sExecutor,
    Singleton<std::shared_ptr<DefaultExe>>& sDefaultExecutor,
    Singleton<RWSpinLock, LockTag>& sExecutorLock) {
  std::shared_ptr<Exe> executor;
  auto singleton = sExecutor.try_get();
  auto lock = sExecutorLock.try_get();

  {
    RWSpinLock::ReadHolder guard(lock.get());
    if ((executor = sExecutor.try_get()->lock())) {
      return executor;
    }
  }

  RWSpinLock::WriteHolder guard(lock.get());
  executor = singleton->lock();
  if (!executor) {
    std::weak_ptr<Exe> defaultExecutor = *sDefaultExecutor.try_get().get();
    executor = defaultExecutor.lock();
    sExecutor.try_get().get()->swap(defaultExecutor);
  }
  return executor;
}

template <class Exe, class LockTag>
void setExecutor(
    std::weak_ptr<Exe> executor,
    Singleton<std::weak_ptr<Exe>>& sExecutor,
    Singleton<RWSpinLock, LockTag>& sExecutorLock) {
  auto lock = sExecutorLock.try_get();
  RWSpinLock::WriteHolder guard(*lock);
  std::weak_ptr<Exe> executor_weak = std::move(executor);
  sExecutor.try_get().get()->swap(executor_weak);
}

std::shared_ptr<Executor> getCPUExecutor() {
  return getExecutor(
      globalCPUExecutor, globalInlineExecutor, globalCPUExecutorLock);
}

void setCPUExecutor(std::weak_ptr<Executor> executor) {
  setExecutor(std::move(executor), globalCPUExecutor, globalCPUExecutorLock);
}

std::shared_ptr<IOExecutor> getIOExecutor() {
  return getExecutor(
      globalIOExecutor, globalIOThreadPool, globalIOExecutorLock);
}

EventBase* getEventBase() {
  return getIOExecutor()->getEventBase();
}

void setIOExecutor(std::weak_ptr<IOExecutor> executor) {
  setExecutor(std::move(executor), globalIOExecutor, globalIOExecutorLock);
}

} // namespace folly
