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

#pragma once

#include <glog/logging.h>
#include <folly/Function.h>

/**
 * Wrappers for different versions of boost::context library
 * API reference for different versions
 * Boost 1.61:
 * https://github.com/boostorg/context/blob/boost-1.61.0/include/boost/context/detail/fcontext.hpp
 *
 * On Windows ARM64, boost::context assembly stubs (jump_fcontext / make_fcontext)
 * are not currently supported, so fall back to the native Windows Fibers API.
 */

#if defined(_WIN32) && defined(_M_ARM64)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace folly {
namespace fibers {

class FiberImpl {
 public:
  FiberImpl(
      folly::Function<void()> func,
      unsigned char* /*stackLimit*/,
      size_t stackSize)
      : func_(std::move(func)) {
    fiber_ = CreateFiberEx(
    stackSize,             
    stackSize,            
    FIBER_FLAG_FLOAT_SWITCH,
    &FiberImpl::fiberFunc,
    this);
    CHECK(fiber_ != nullptr)
    << "CreateFiberEx failed: " << GetLastError();
  }

  ~FiberImpl() {
      if (fiber_) {
          DCHECK(GetCurrentFiber() != fiber_)
              << "Destroying fiber while it is active";
          DeleteFiber(std::exchange(fiber_, nullptr));
      }
  }

  FiberImpl(const FiberImpl&) = delete;
  FiberImpl& operator=(const FiberImpl&) = delete;

  FiberImpl(FiberImpl&& other) noexcept
      : func_(std::move(other.func_)),
        fiber_(std::exchange(other.fiber_, nullptr)),
        mainFiber_(std::exchange(other.mainFiber_, nullptr)),
        convertedThread_(std::exchange(other.convertedThread_, false)) {}

  FiberImpl& operator=(FiberImpl&& other) noexcept {
    if (this != &other) {
      if (fiber_) DeleteFiber(fiber_);
      func_ = std::move(other.func_);
      fiber_ = std::exchange(other.fiber_, nullptr);
      mainFiber_ = std::exchange(other.mainFiber_, nullptr);
      convertedThread_ = std::exchange(other.convertedThread_, false);
    }
    return *this;
  }

  void activate() {
      mainFiber_ = GetCurrentFiber();
      // On ARM64 Windows, GetCurrentFiber() returns a garbage low address
      // (e.g. 0x1E00) when the thread has not been converted to a fiber yet.
      // A real fiber handle is always above 64KB (Windows minimum allocation
      // granularity), so use 0x10000 as the threshold.
      if (mainFiber_ == nullptr ||
          mainFiber_ == INVALID_HANDLE_VALUE ||
          reinterpret_cast<uintptr_t>(mainFiber_) < 0x10000) {
          mainFiber_ = ConvertThreadToFiber(nullptr);
          CHECK(mainFiber_ != nullptr)
              << "ConvertThreadToFiber failed: " << GetLastError();
          convertedThread_ = true;
      }
      SwitchToFiber(fiber_);
  }


  void deactivate() {
    DCHECK(mainFiber_ != nullptr) << "deactivate() called before activate()";
    SwitchToFiber(std::exchange(mainFiber_, nullptr));
    if (convertedThread_) {
        ConvertFiberToThread();
        convertedThread_ = false;
    }
  }

  void* getStackPointer() const { return nullptr; }

 private:
  static VOID CALLBACK fiberFunc(LPVOID param) {
    auto* self = static_cast<FiberImpl*>(param);
    self->func_();
    // func_() must not return in normal folly::fibers usage.
    // If it does, switch back to avoid undefined behavior — but
    // don't call deactivate() as mainFiber_ may already be cleared.
    CHECK(false) << "FiberImpl::func_() returned unexpectedly";
  }

  folly::Function<void()> func_;
  LPVOID fiber_{nullptr};
  LPVOID mainFiber_{nullptr};
  bool convertedThread_{false};
};

} // namespace fibers
} // namespace folly
#else

#include <boost/context/detail/fcontext.hpp>

#if defined(__clang__)
#define FOLLY_DETAIL_DISABLE_TAIL_CALLS __attribute__((disable_tail_calls))
#define FOLLY_DETAIL_TAIL_CALLS_CAN_BE_DISABLED 1
#elif defined(__GNUC__)
#define FOLLY_DETAIL_DISABLE_TAIL_CALLS \
  __attribute__((optimize("no-optimize-sibling-calls")))
#define FOLLY_DETAIL_TAIL_CALLS_CAN_BE_DISABLED 1
#else
#define FOLLY_DETAIL_DISABLE_TAIL_CALLS
#define FOLLY_DETAIL_TAIL_CALLS_CAN_BE_DISABLED 0
#endif

namespace folly {
namespace fibers {

class FiberImpl {
  using FiberContext = boost::context::detail::fcontext_t;

  using MainContext = boost::context::detail::fcontext_t;

 public:
  FiberImpl(
      folly::Function<void()> func, unsigned char* stackLimit, size_t stackSize)
      : func_(std::move(func)) {
    auto stackBase = stackLimit + stackSize;
    entryFrameBase_ = nullptr;
    fiberContext_ =
        boost::context::detail::make_fcontext(stackBase, stackSize, &fiberFunc);
  }

  void activate() {
    auto transfer = boost::context::detail::jump_fcontext(fiberContext_, this);
    fiberContext_ = transfer.fctx;
    auto context = reinterpret_cast<intptr_t>(transfer.data);
    DCHECK_EQ(0, context);
  }

  void deactivate() {
    auto transfer =
        boost::context::detail::jump_fcontext(mainContext_, nullptr);
    mainContext_ = transfer.fctx;
    fixStackUnwinding();
    auto context = reinterpret_cast<intptr_t>(transfer.data);
    DCHECK_EQ(this, reinterpret_cast<FiberImpl*>(context));
  }

  void* getStackPointer() const {
    if constexpr (kIsLinux || kIsApple) {
      if constexpr (kIsArchAmd64) {
        // RBP at offset 0x30 (index 6) in the SysV x86_64 fcontext layout.
        return reinterpret_cast<void**>(fiberContext_)[6];
      } else if constexpr (kIsArchAArch64) {
        // FP (x29) at offset 0x90 (index 18) in the arm64 fcontext layout.
        return reinterpret_cast<void**>(fiberContext_)[18];
      }
    }
    return nullptr;
  }

 private:
  static FOLLY_NOINLINE FOLLY_DETAIL_DISABLE_TAIL_CALLS void fiberFunc(
      boost::context::detail::transfer_t transfer) {
    auto fiberImpl = reinterpret_cast<FiberImpl*>(transfer.data);
    fiberImpl->mainContext_ = transfer.fctx;
#if FOLLY_HAS_BUILTIN(__builtin_frame_address) && \
    FOLLY_DETAIL_TAIL_CALLS_CAN_BE_DISABLED
    fiberImpl->entryFrameBase_ = __builtin_frame_address(0);
#endif
    fiberImpl->fixStackUnwinding();
    fiberImpl->func_();
  }

  void fixStackUnwinding() {
    if (kIsLinux || kIsApple) {
      // Stitch main context stack and fiber stack so that frame-pointer-based
      // stack walkers can terminate cleanly at the fiber boundary on the
      // ELF and Mach-O fcontext backends.
      auto frameBase = reinterpret_cast<void**>(entryFrameBase_);
      if (frameBase == nullptr) {
        return;
      }
      auto mainContext = reinterpret_cast<void**>(mainContext_);
      if constexpr (kIsArchAmd64) {
        // Extract RBP and RIP from main context (offsets 6 and 7).
        frameBase[0] = mainContext[6];
        frameBase[1] = mainContext[7];
      } else if constexpr (kIsArchAArch64) {
        // Extract FP (x29) and LR (x30) from main context
        // (offsets 0x90 and 0x98 in the fcontext layout).
        frameBase[0] = mainContext[18];
        frameBase[1] = mainContext[19];
      }
    }
  }

  void* entryFrameBase_;
  folly::Function<void()> func_;
  FiberContext fiberContext_;
  MainContext mainContext_;
};
} // namespace fibers
} // namespace folly

#undef FOLLY_DETAIL_TAIL_CALLS_CAN_BE_DISABLED
#undef FOLLY_DETAIL_DISABLE_TAIL_CALLS

#endif

