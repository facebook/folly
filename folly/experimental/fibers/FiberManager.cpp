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
#include "FiberManager.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <cassert>
#include <stdexcept>

#include <glog/logging.h>

#include <folly/experimental/fibers/Fiber.h>
#include <folly/experimental/fibers/LoopController.h>

namespace folly { namespace fibers {

FOLLY_TLS FiberManager* FiberManager::currentFiberManager_ = nullptr;

FiberManager::FiberManager(std::unique_ptr<LoopController> loopController,
                           Options options) :
    FiberManager(LocalType<void>(),
                 std::move(loopController),
                 std::move(options)) {}

FiberManager::~FiberManager() {
  if (isLoopScheduled_) {
    loopController_->cancel();
  }

  while (!fibersPool_.empty()) {
    fibersPool_.pop_front_and_dispose([] (Fiber* fiber) {
      delete fiber;
    });
  }
  assert(readyFibers_.empty());
  assert(fibersActive_ == 0);
}

LoopController& FiberManager::loopController() {
  return *loopController_;
}

const LoopController& FiberManager::loopController() const {
  return *loopController_;
}

bool FiberManager::hasTasks() const {
  return fibersActive_ > 0 ||
         !remoteReadyQueue_.empty() ||
         !remoteTaskQueue_.empty();
}

Fiber* FiberManager::getFiber() {
  Fiber* fiber = nullptr;

  if (options_.fibersPoolResizePeriodMs > 0 && !fibersPoolResizerScheduled_) {
    fibersPoolResizer_();
    fibersPoolResizerScheduled_ = true;
  }

  if (fibersPool_.empty()) {
    fiber = new Fiber(*this);
    ++fibersAllocated_;
  } else {
    fiber = &fibersPool_.front();
    fibersPool_.pop_front();
    assert(fibersPoolSize_ > 0);
    --fibersPoolSize_;
  }
  assert(fiber);
  if (++fibersActive_ > maxFibersActiveLastPeriod_) {
    maxFibersActiveLastPeriod_ = fibersActive_;
  }
  ++fiberId_;
  bool recordStack = (options_.recordStackEvery != 0) &&
                     (fiberId_ % options_.recordStackEvery == 0);
  fiber->init(recordStack);
  return fiber;
}

void FiberManager::setExceptionCallback(FiberManager::ExceptionCallback ec) {
  assert(ec);
  exceptionCallback_ = std::move(ec);
}

size_t FiberManager::fibersAllocated() const {
  return fibersAllocated_;
}

size_t FiberManager::fibersPoolSize() const {
  return fibersPoolSize_;
}

size_t FiberManager::stackHighWatermark() const {
  return stackHighWatermark_;
}

void FiberManager::remoteReadyInsert(Fiber* fiber) {
  if (observer_) {
    observer_->runnable(reinterpret_cast<uintptr_t>(fiber));
  }
  if (remoteReadyQueue_.insertHead(fiber)) {
    loopController_->scheduleThreadSafe();
  }
}

void FiberManager::setObserver(ExecutionObserver* observer) {
  observer_ = observer;
}

void FiberManager::doFibersPoolResizing() {
  while (fibersAllocated_ > maxFibersActiveLastPeriod_ &&
         fibersPoolSize_ > options_.maxFibersPoolSize) {
    auto fiber = &fibersPool_.front();
    assert(fiber != nullptr);
    fibersPool_.pop_front();
    delete fiber;
    --fibersPoolSize_;
    --fibersAllocated_;
  }

  maxFibersActiveLastPeriod_ = fibersActive_;
}

void FiberManager::FiberManager::FibersPoolResizer::operator()() {
  fiberManager_.doFibersPoolResizing();
  fiberManager_.timeoutManager_->registerTimeout(
      *this,
      std::chrono::milliseconds(
        fiberManager_.options_.fibersPoolResizePeriodMs));
}

}}
