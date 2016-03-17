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
#include "FiberManager.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <cassert>
#include <stdexcept>

#include <glog/logging.h>

#include <folly/experimental/fibers/Fiber.h>
#include <folly/experimental/fibers/LoopController.h>

#ifdef FOLLY_SANITIZE_ADDRESS

#include <dlfcn.h>

static void __asan_enter_fiber_weak(
    void const* fiber_stack_base,
    size_t fiber_stack_extent)
    __attribute__((__weakref__("__asan_enter_fiber")));
static void __asan_exit_fiber_weak()
    __attribute__((__weakref__("__asan_exit_fiber")));

typedef void (*AsanEnterFiberFuncPtr)(void const*, size_t);
typedef void (*AsanExitFiberFuncPtr)();

namespace folly { namespace fibers {

static AsanEnterFiberFuncPtr getEnterFiberFunc();
static AsanExitFiberFuncPtr getExitFiberFunc();

}}

#endif

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
  auto insertHead = [&]() { return remoteReadyQueue_.insertHead(fiber); };
  loopController_->scheduleThreadSafe(std::ref(insertHead));
}

void FiberManager::setObserver(ExecutionObserver* observer) {
  observer_ = observer;
}

void FiberManager::setPreemptRunner(InlineFunctionRunner* preemptRunner) {
  preemptRunner_ = preemptRunner;
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

void FiberManager::FibersPoolResizer::operator()() {
  fiberManager_.doFibersPoolResizing();
  fiberManager_.timeoutManager_->registerTimeout(
      *this,
      std::chrono::milliseconds(
        fiberManager_.options_.fibersPoolResizePeriodMs));
}

#ifdef FOLLY_SANITIZE_ADDRESS

void FiberManager::registerFiberActivationWithAsan(Fiber* fiber) {
  auto context = &fiber->fcontext_;
  void* top = context->stackBase();
  void* bottom = context->stackLimit();
  size_t extent = static_cast<char*>(top) - static_cast<char*>(bottom);

  // Check if we can find a fiber enter function and call it if we find one
  static AsanEnterFiberFuncPtr fn = getEnterFiberFunc();
  if (fn == nullptr) {
    LOG(FATAL) << "The version of ASAN in use doesn't support fibers";
  } else {
    fn(bottom, extent);
  }
}

void FiberManager::registerFiberDeactivationWithAsan(Fiber* fiber) {
  (void)fiber; // currently unused

  // Check if we can find a fiber exit function and call it if we find one
  static AsanExitFiberFuncPtr fn = getExitFiberFunc();
  if (fn == nullptr) {
    LOG(FATAL) << "The version of ASAN in use doesn't support fibers";
  } else {
    fn();
  }
}

static AsanEnterFiberFuncPtr getEnterFiberFunc() {
  AsanEnterFiberFuncPtr fn{nullptr};

  // Check whether weak reference points to statically linked enter function
  if (nullptr != (fn = &::__asan_enter_fiber_weak)) {
    return fn;
  }

  // Check whether we can find a dynamically linked enter function
  if (nullptr !=
      (fn = (AsanEnterFiberFuncPtr)dlsym(RTLD_DEFAULT, "__asan_enter_fiber"))) {
    return fn;
  }

  // Couldn't find the function at all
  return nullptr;
}

static AsanExitFiberFuncPtr getExitFiberFunc() {
  AsanExitFiberFuncPtr fn{nullptr};

  // Check whether weak reference points to statically linked exit function
  if (nullptr != (fn = &::__asan_exit_fiber_weak)) {
    return fn;
  }

  // Check whether we can find a dynamically linked enter function
  if (nullptr !=
      (fn = (AsanExitFiberFuncPtr)dlsym(RTLD_DEFAULT, "__asan_exit_fiber"))) {
    return fn;
  }

  // Couldn't find the function at all
  return nullptr;
}

#endif // FOLLY_SANITIZE_ADDRESS
}}
