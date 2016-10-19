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
#pragma once

#include <boost/context/fcontext.hpp>
#include <boost/version.hpp>

/**
 * Wrappers for different versions of boost::context library
 * API reference for different versions
 * Boost 1.51:
 * http://www.boost.org/doc/libs/1_51_0/libs/context/doc/html/context/context/boost_fcontext.html
 * Boost 1.52:
 * http://www.boost.org/doc/libs/1_52_0/libs/context/doc/html/context/context/boost_fcontext.html
 * Boost 1.56:
 * http://www.boost.org/doc/libs/1_56_0/libs/context/doc/html/context/context/boost_fcontext.html
 */

namespace folly {
namespace fibers {

struct FContext {
 public:
#if BOOST_VERSION >= 105200
  using ContextStruct = boost::context::fcontext_t;
#else
  using ContextStruct = boost::ctx::fcontext_t;
#endif

  void* stackLimit() const {
    return stackLimit_;
  }

  void* stackBase() const {
    return stackBase_;
  }

 private:
  void* stackLimit_;
  void* stackBase_;

#if BOOST_VERSION >= 105600
  ContextStruct context_;
#elif BOOST_VERSION >= 105200
  ContextStruct* context_;
#else
  ContextStruct context_;
#endif

  friend intptr_t
  jumpContext(FContext* oldC, FContext::ContextStruct* newC, intptr_t p);
  friend intptr_t
  jumpContext(FContext::ContextStruct* oldC, FContext* newC, intptr_t p);
  friend FContext
  makeContext(void* stackLimit, size_t stackSize, void (*fn)(intptr_t));
};

inline intptr_t
jumpContext(FContext* oldC, FContext::ContextStruct* newC, intptr_t p) {
#if BOOST_VERSION >= 105600
  return boost::context::jump_fcontext(&oldC->context_, *newC, p);
#elif BOOST_VERSION >= 105200
  return boost::context::jump_fcontext(oldC->context_, newC, p);
#else
  return jump_fcontext(&oldC->context_, newC, p);
#endif
}

inline intptr_t
jumpContext(FContext::ContextStruct* oldC, FContext* newC, intptr_t p) {
#if BOOST_VERSION >= 105200
  return boost::context::jump_fcontext(oldC, newC->context_, p);
#else
  return jump_fcontext(oldC, &newC->context_, p);
#endif
}

inline FContext
makeContext(void* stackLimit, size_t stackSize, void (*fn)(intptr_t)) {
  FContext res;
  res.stackLimit_ = stackLimit;
  res.stackBase_ = static_cast<unsigned char*>(stackLimit) + stackSize;

#if BOOST_VERSION >= 105200
  res.context_ = boost::context::make_fcontext(res.stackBase_, stackSize, fn);
#else
  res.context_.fc_stack.limit = stackLimit;
  res.context_.fc_stack.base = res.stackBase_;
  make_fcontext(&res.context_, fn);
#endif

  return res;
}
}
} // folly::fibers
