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

#include <cassert>

namespace folly {
namespace fibers {

template <typename F>
void Fiber::setFunction(F&& func, TaskOptions taskOptions) {
  assert(state_ == INVALID);
  func_ = std::forward<F>(func);
  state_ = NOT_STARTED;
  taskOptions_ = std::move(taskOptions);
}

template <typename F, typename G>
void Fiber::setFunctionFinally(F&& resultFunc, G&& finallyFunc) {
  assert(state_ == INVALID);
  resultFunc_ = std::forward<F>(resultFunc);
  finallyFunc_ = std::forward<G>(finallyFunc);
  state_ = NOT_STARTED;
  taskOptions_ = TaskOptions();
}

inline void* Fiber::getUserBuffer() {
  return &userBuffer_;
}

template <typename T>
T& Fiber::LocalData::getSlow() {
  vtable_ = VTable::get<T>();
  T* data = nullptr;
  if FOLLY_CXX17_CONSTEXPR (
      sizeof(T) <= sizeof(Buffer) && alignof(T) <= alignof(Buffer)) {
    data = new (&buffer_) T();
  } else {
    data = new T();
  }
  data_ = data;
  return *data;
}

} // namespace fibers
} // namespace folly
