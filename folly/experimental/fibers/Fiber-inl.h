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
#pragma once

#include <cassert>

namespace folly { namespace fibers {

template <typename F>
void Fiber::setFunction(F&& func) {
  assert(state_ == INVALID);
  func_ = std::move(func);
  state_ = NOT_STARTED;
}

template <typename F, typename G>
void Fiber::setFunctionFinally(F&& resultFunc,
                               G&& finallyFunc) {
  assert(state_ == INVALID);
  resultFunc_ = std::move(resultFunc);
  finallyFunc_ = std::move(finallyFunc);
  state_ = NOT_STARTED;
}

inline void* Fiber::getUserBuffer() {
  return &userBuffer_;
}

template <typename G>
void Fiber::setReadyFunction(G&& func) {
  assert(state_ == INVALID || state_ == NOT_STARTED);
  readyFunc_ = std::move(func);
}

}}  // folly::fibers
