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
#include "Fiber.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/experimental/fibers/BoostContextCompatibility.h>
#include <folly/experimental/fibers/FiberManager.h>

namespace folly { namespace fibers {

namespace {
static const uint64_t kMagic8Bytes = 0xfaceb00cfaceb00c;

pid_t localThreadId() {
  static thread_local pid_t threadId = syscall(SYS_gettid);
  return threadId;
}

/* Size of the region from p + nBytes down to the last non-magic value */
static size_t nonMagicInBytes(const FContext& context) {
  uint64_t* begin = static_cast<uint64_t*>(context.stackLimit());
  uint64_t* end = static_cast<uint64_t*>(context.stackBase());

  auto firstNonMagic = std::find_if(
    begin, end,
    [](uint64_t val) {
      return val != kMagic8Bytes;
    }
  );

  return (end - firstNonMagic) * sizeof(uint64_t);
}

}  // anonymous namespace

void Fiber::setData(intptr_t data) {
  assert(state_ == AWAITING);
  data_ = data;
  state_ = READY_TO_RUN;

  if (LIKELY(threadId_ == localThreadId())) {
    fiberManager_.readyFibers_.push_back(*this);
    fiberManager_.ensureLoopScheduled();
  } else {
    fiberManager_.remoteReadyInsert(this);
  }
}

Fiber::Fiber(FiberManager& fiberManager) :
    fiberManager_(fiberManager) {

  auto size = fiberManager_.options_.stackSize;
  auto limit = fiberManager_.stackAllocator_.allocate(size);

  fcontext_ = makeContext(limit, size, &Fiber::fiberFuncHelper);
}

void Fiber::init(bool recordStackUsed) {
  recordStackUsed_ = recordStackUsed;
  if (UNLIKELY(recordStackUsed_ && !stackFilledWithMagic_)) {
    auto limit = fcontext_.stackLimit();
    auto base = fcontext_.stackBase();

    std::fill(static_cast<uint64_t*>(limit),
              static_cast<uint64_t*>(base),
              kMagic8Bytes);

    // newer versions of boost allocate context on fiber stack,
    // need to create a new one
    auto size = fiberManager_.options_.stackSize;
    fcontext_ = makeContext(limit, size, &Fiber::fiberFuncHelper);

    stackFilledWithMagic_ = true;
  }
}

Fiber::~Fiber() {
  fiberManager_.stackAllocator_.deallocate(
    static_cast<unsigned char*>(fcontext_.stackLimit()),
    fiberManager_.options_.stackSize);
}

void Fiber::recordStackPosition() {
  int stackDummy;
  auto currentPosition = static_cast<size_t>(
     static_cast<unsigned char*>(fcontext_.stackBase()) -
     static_cast<unsigned char*>(static_cast<void*>(&stackDummy)));
  fiberManager_.stackHighWatermark_ =
    std::max(fiberManager_.stackHighWatermark_, currentPosition);
  VLOG(4) << "Stack usage: " << currentPosition;
}

void Fiber::fiberFuncHelper(intptr_t fiber) {
  reinterpret_cast<Fiber*>(fiber)->fiberFunc();
}

/*
 * Some weird bug in ASAN causes fiberFunc to allocate boundless amounts of
 * memory inside __asan_handle_no_return.  Work around this in ASAN builds by
 * tricking the compiler into thinking it may, someday, return.
 */
#ifdef FOLLY_SANITIZE_ADDRESS
volatile bool loopForever = true;
#else
static constexpr bool loopForever = true;
#endif

void Fiber::fiberFunc() {
  while (loopForever) {
    assert(state_ == NOT_STARTED);

    threadId_ = localThreadId();
    state_ = RUNNING;

    try {
      if (resultFunc_) {
        assert(finallyFunc_);
        assert(!func_);

        resultFunc_();
      } else {
        assert(func_);
        func_();
      }
    } catch (...) {
      fiberManager_.exceptionCallback_(std::current_exception(),
                                       "running Fiber func_/resultFunc_");
    }

    if (UNLIKELY(recordStackUsed_)) {
      fiberManager_.stackHighWatermark_ =
        std::max(fiberManager_.stackHighWatermark_,
                 nonMagicInBytes(fcontext_));
      VLOG(3) << "Max stack usage: " << fiberManager_.stackHighWatermark_;
      CHECK(fiberManager_.stackHighWatermark_ <
              fiberManager_.options_.stackSize - 64) << "Fiber stack overflow";
    }

    state_ = INVALID;

    fiberManager_.activeFiber_ = nullptr;

    auto fiber = reinterpret_cast<Fiber*>(
      jumpContext(&fcontext_, &fiberManager_.mainContext_, 0));
    assert(fiber == this);
  }
}

intptr_t Fiber::preempt(State state) {
  assert(fiberManager_.activeFiber_ == this);
  assert(state_ == RUNNING);
  assert(state != RUNNING);

  fiberManager_.activeFiber_ = nullptr;
  state_ = state;

  recordStackPosition();

  auto ret = jumpContext(&fcontext_, &fiberManager_.mainContext_, 0);

  assert(fiberManager_.activeFiber_ == this);
  assert(state_ == READY_TO_RUN);
  state_ = RUNNING;

  return ret;
}

Fiber::LocalData::LocalData(const LocalData& other) : data_(nullptr) {
  *this = other;
}

Fiber::LocalData& Fiber::LocalData::operator=(const LocalData& other) {
  reset();
  if (!other.data_) {
    return *this;
  }

  dataSize_ = other.dataSize_;
  dataType_ = other.dataType_;
  dataDestructor_ = other.dataDestructor_;
  dataCopyConstructor_ = other.dataCopyConstructor_;

  if (dataSize_ <= kBufferSize) {
    data_ = &buffer_;
  } else {
    data_ = allocateHeapBuffer(dataSize_);
  }

  dataCopyConstructor_(data_, other.data_);

  return *this;
}

void Fiber::LocalData::reset() {
  if (!data_) {
    return;
  }

  dataDestructor_(data_);
  data_ = nullptr;
}

void* Fiber::LocalData::allocateHeapBuffer(size_t size) {
  return new char[size];
}

void Fiber::LocalData::freeHeapBuffer(void* buffer) {
  delete[] reinterpret_cast<char*>(buffer);
}

}}
