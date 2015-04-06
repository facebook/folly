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

__thread FiberManager* FiberManager::currentFiberManager_ = nullptr;

FiberManager::FiberManager(std::unique_ptr<LoopController> loopController,
                           Options options) :
    loopController_(std::move(loopController)),
    options_(options),
    exceptionCallback_([](std::exception_ptr e, std::string context) {
        try {
          std::rethrow_exception(e);
        } catch (const std::exception& e) {
          LOG(DFATAL) << "Exception " << typeid(e).name()
                      << " with message '" << e.what() << "' was thrown in "
                      << "FiberManager with context '" << context << "'";
          throw;
        } catch (...) {
          LOG(DFATAL) << "Unknown exception was thrown in FiberManager with "
                      << "context '" << context << "'";
          throw;
        }
      }),
    timeoutManager_(std::make_shared<TimeoutController>(*loopController_)),
    localType_(typeid(void)) {
  loopController_->setFiberManager(this);
}

FiberManager::~FiberManager() {
  if (isLoopScheduled_) {
    loopController_->cancel();
  }

  Fiber* fiberIt;
  Fiber* fiberItNext;
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
  ++fibersActive_;
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
  if (remoteReadyQueue_.insertHead(fiber)) {
    loopController_->scheduleThreadSafe();
  }
}

}}
