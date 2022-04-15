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

#include <folly/fibers/FiberManagerInternal.h>

#include <csignal>

#include <cassert>
#include <stdexcept>

#include <glog/logging.h>

#include <folly/fibers/Fiber.h>
#include <folly/fibers/LoopController.h>

#include <folly/ConstexprMath.h>
#include <folly/SingletonThreadLocal.h>
#include <folly/memory/SanitizeAddress.h>
#include <folly/portability/Config.h>
#include <folly/portability/SysSyscall.h>
#include <folly/portability/Unistd.h>
#include <folly/synchronization/SanitizeThread.h>

namespace std {
template <>
struct hash<folly::fibers::FiberManager::Options> {
  ssize_t operator()(const folly::fibers::FiberManager::Options& opts) const {
    return hash<decltype(opts.hash())>()(opts.hash());
  }
};
} // namespace std

namespace folly {
namespace fibers {

auto FiberManager::FrozenOptions::create(const Options& options) -> ssize_t {
  return std::hash<Options>()(options);
}

/* static */ FiberManager*& FiberManager::getCurrentFiberManager() {
  struct Tag {};
  folly::annotate_ignore_thread_sanitizer_guard g(__FILE__, __LINE__);
  return SingletonThreadLocal<FiberManager*, Tag>::get();
}

FiberManager::FiberManager(
    std::unique_ptr<LoopController> loopController, Options options)
    : FiberManager(LocalType<void>(), std::move(loopController), options) {}

FiberManager::~FiberManager() {
  loopController_.reset();

  while (!fibersPool_.empty()) {
    fibersPool_.pop_front_and_dispose([](Fiber* fiber) { delete fiber; });
  }
  assert(readyFibers_.empty());
  assert(!hasTasks());
}

LoopController& FiberManager::loopController() {
  return *loopController_;
}

const LoopController& FiberManager::loopController() const {
  return *loopController_;
}

bool FiberManager::hasTasks() const {
  return fibersActive_.load(std::memory_order_relaxed) > 0 ||
      !remoteReadyQueue_.empty() || !remoteTaskQueue_.empty() ||
      remoteCount_ > 0;
}

bool FiberManager::isRemoteScheduled() const {
  return remoteCount_ > 0;
}

Fiber* FiberManager::getFiber() {
  Fiber* fiber = nullptr;

  if (options_.fibersPoolResizePeriodMs > 0 && !fibersPoolResizerScheduled_) {
    fibersPoolResizer_.run();
    fibersPoolResizerScheduled_ = true;
  }

  if (fibersPool_.empty()) {
    fiber = new Fiber(*this);
    fibersAllocated_.store(fibersAllocated() + 1, std::memory_order_relaxed);
  } else {
    fiber = &fibersPool_.front();
    fibersPool_.pop_front();
    auto fibersPoolSize = fibersPoolSize_.load(std::memory_order_relaxed);
    assert(fibersPoolSize > 0);
    fibersPoolSize_.store(fibersPoolSize - 1, std::memory_order_relaxed);
  }
  assert(fiber);
  auto active = 1 + fibersActive_.fetch_add(1, std::memory_order_relaxed);
  if (active > maxFibersActiveLastPeriod_) {
    maxFibersActiveLastPeriod_ = active;
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
  return fibersAllocated_.load(std::memory_order_relaxed);
}

size_t FiberManager::fibersPoolSize() const {
  return fibersPoolSize_.load(std::memory_order_relaxed);
}

size_t FiberManager::stackHighWatermark() const {
  return stackHighWatermark_.load(std::memory_order_relaxed);
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

ExecutionObserver* FiberManager::getObserver() {
  return observer_;
}

void FiberManager::setPreemptRunner(InlineFunctionRunner* preemptRunner) {
  preemptRunner_ = preemptRunner;
}

void FiberManager::doFibersPoolResizing() {
  while (true) {
    auto fibersAllocated = this->fibersAllocated();
    auto fibersPoolSize = this->fibersPoolSize();
    if (!(fibersAllocated > maxFibersActiveLastPeriod_ &&
          fibersPoolSize > options_.maxFibersPoolSize)) {
      break;
    }
    auto fiber = &fibersPool_.front();
    assert(fiber != nullptr);
    fibersPool_.pop_front();
    delete fiber;
    fibersPoolSize_.store(fibersPoolSize - 1, std::memory_order_relaxed);
    fibersAllocated_.store(fibersAllocated - 1, std::memory_order_relaxed);
  }

  maxFibersActiveLastPeriod_ = fibersActive_.load(std::memory_order_relaxed);
}

void FiberManager::FibersPoolResizer::run() {
  fiberManager_.doFibersPoolResizing();
  if (auto timer = fiberManager_.loopController_->timer()) {
    RequestContextScopeGuard rctxGuard(std::shared_ptr<RequestContext>{});
    timer->scheduleTimeout(
        this,
        std::chrono::milliseconds(
            fiberManager_.options_.fibersPoolResizePeriodMs));
  }
}

void FiberManager::registerStartSwitchStackWithAsan(
    void** saveFakeStack, const void* stackBottom, size_t stackSize) {
  sanitizer_start_switch_fiber(saveFakeStack, stackBottom, stackSize);
}

void FiberManager::registerFinishSwitchStackWithAsan(
    void* saveFakeStack, const void** saveStackBottom, size_t* saveStackSize) {
  sanitizer_finish_switch_fiber(saveFakeStack, saveStackBottom, saveStackSize);
}

void FiberManager::freeFakeStack(void* fakeStack) {
  void* saveFakeStack;
  const void* stackBottom;
  size_t stackSize;
  sanitizer_start_switch_fiber(&saveFakeStack, nullptr, 0);
  sanitizer_finish_switch_fiber(fakeStack, &stackBottom, &stackSize);
  sanitizer_start_switch_fiber(nullptr, stackBottom, stackSize);
  sanitizer_finish_switch_fiber(saveFakeStack, nullptr, nullptr);
}

void FiberManager::unpoisonFiberStack(const Fiber* fiber) {
  auto stack = fiber->getStack();
  asan_unpoison_memory_region(stack.first, stack.second);
}

// TVOS and WatchOS platforms have SIGSTKSZ but not sigaltstack
#if defined(SIGSTKSZ) && !FOLLY_APPLE_TVOS && !FOLLY_APPLE_WATCHOS

namespace {

bool hasAlternateStack() {
  stack_t ss;
  sigaltstack(nullptr, &ss);
  return !(ss.ss_flags & SS_DISABLE);
}

int setAlternateStack(char* sp, size_t size) {
  CHECK(sp);
  stack_t ss{};
  ss.ss_sp = sp;
  ss.ss_size = size;
  return sigaltstack(&ss, nullptr);
}

int unsetAlternateStack() {
  stack_t ss{};
  ss.ss_flags = SS_DISABLE;
  return sigaltstack(&ss, nullptr);
}

class ScopedAlternateSignalStack {
 public:
  ScopedAlternateSignalStack() {
    if (hasAlternateStack()) {
      return;
    }

    // SIGSTKSZ (8 kB on our architectures) isn't always enough for
    // folly::symbolizer, so allocate 32 kB.
    size_t kAltStackSize = std::max(size_t(SIGSTKSZ), size_t(32 * 1024));

    stack_ = std::unique_ptr<char[]>(new char[kAltStackSize]);

    setAlternateStack(stack_.get(), kAltStackSize);
  }

  ScopedAlternateSignalStack(ScopedAlternateSignalStack&&) = default;
  ScopedAlternateSignalStack& operator=(ScopedAlternateSignalStack&&) = default;

  ~ScopedAlternateSignalStack() {
    if (stack_) {
      unsetAlternateStack();
    }
  }

 private:
  std::unique_ptr<char[]> stack_;
};
} // namespace

void FiberManager::maybeRegisterAlternateSignalStack() {
  SingletonThreadLocal<ScopedAlternateSignalStack>::get();

  alternateSignalStackRegistered_ = true;
}

#else

void FiberManager::maybeRegisterAlternateSignalStack() {
  // no-op
}

#endif

} // namespace fibers
} // namespace folly
