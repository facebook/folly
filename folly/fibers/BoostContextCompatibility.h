/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

 private:
  static void fiberFunc(boost::context::detail::transfer_t transfer) {
    auto fiberImpl = reinterpret_cast<FiberImpl*>(transfer.data);
    fiberImpl->mainContext_ = transfer.fctx;
    fiberImpl->fixStackUnwinding();
    fiberImpl->func_();
  }

  void fixStackUnwinding() {
    if (kIsArchAmd64 && kIsLinux) {
      // Extract RBP and RIP from main context to stitch main context stack and
      // fiber stack.
      auto stackBase = reinterpret_cast<void**>(stackBase_);
      auto mainContext = reinterpret_cast<void**>(mainContext_);
      stackBase[-2] = mainContext[6];
      stackBase[-1] = mainContext[7];
    }
  }

  unsigned char* stackBase_;
  folly::Function<void()> func_;
  FiberContext fiberContext_;
  MainContext mainContext_;
};
} // namespace fibers
} // namespace folly
