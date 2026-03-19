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

#include <boost/context/detail/fcontext.hpp>
#include <glog/logging.h>

/**
 * Wrappers for different versions of boost::context library
 * API reference for different versions
 * Boost 1.61:
 * https://github.com/boostorg/context/blob/boost-1.61.0/include/boost/context/detail/fcontext.hpp
 */

#include <folly/Function.h>

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
    stackBase_ = stackBase;
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
    if constexpr (kIsLinux) {
      if constexpr (kIsArchAmd64) {
        // RBP at offset 0x30 (index 6) in the x86_64 fcontext layout.
        return reinterpret_cast<void**>(fiberContext_)[6];
      } else if constexpr (kIsArchAArch64) {
        // FP (x29) at offset 0x90 (index 18) in the arm64 fcontext layout.
        return reinterpret_cast<void**>(fiberContext_)[18];
      }
    }
    return nullptr;
  }

 private:
  static void fiberFunc(boost::context::detail::transfer_t transfer) {
    auto fiberImpl = reinterpret_cast<FiberImpl*>(transfer.data);
    fiberImpl->mainContext_ = transfer.fctx;
    fiberImpl->fixStackUnwinding();
    fiberImpl->func_();
  }

  void fixStackUnwinding() {
    if (kIsLinux) {
      // Stitch main context stack and fiber stack so that frame-pointer-based
      // stack walkers (e.g. jemalloc prof_backtrace_impl) can terminate
      // cleanly at the fiber boundary.
      auto stackBase = reinterpret_cast<void**>(stackBase_);
      auto mainContext = reinterpret_cast<void**>(mainContext_);
      if constexpr (kIsArchAmd64) {
        // Extract RBP and RIP from main context (offsets 6 and 7).
        stackBase[-2] = mainContext[6];
        stackBase[-1] = mainContext[7];
      } else if constexpr (kIsArchAArch64) {
        // Extract FP (x29) and LR (x30) from main context
        // (offsets 0x90 and 0x98 in the fcontext layout).
        stackBase[-2] = mainContext[18];
        stackBase[-1] = mainContext[19];
      }
    }
  }

  unsigned char* stackBase_;
  folly::Function<void()> func_;
  FiberContext fiberContext_;
  MainContext mainContext_;
};
} // namespace fibers
} // namespace folly
