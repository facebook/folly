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
#include <cstddef>
#include <cstdint>

#include <folly/Portability.h>

namespace folly {
namespace exception_tracer {

constexpr size_t kMaxFrames = 500;

struct StackTrace {
  StackTrace() : frameCount(0) {}

  size_t frameCount;
  uintptr_t addresses[kMaxFrames];
};

class StackTraceStack {
  class Node;

 public:
  StackTraceStack() = default;

  StackTraceStack(const StackTraceStack&) = delete;
  void operator=(const StackTraceStack&) = delete;

  /**
   * Push the current stack trace onto the stack.
   * Returns false on failure (not enough memory, getting stack trace failed),
   * true on success.
   */
  bool pushCurrent();

  /**
   * Pop the top stack trace from the stack.
   * Returns true on success, false on failure (stack was empty).
   */
  bool pop();

  /**
   * Move the top stack trace from other onto this.
   * Returns true on success, false on failure (other was empty).
   */
  bool moveTopFrom(StackTraceStack& other);

  /**
   * Clear the stack.
   */

  void clear();

  /**
   * Is the stack empty?
   */
  bool empty() const { return !state_; }

  /**
   * Return the top stack trace, or nullptr if the stack is empty.
   */
  StackTrace* top();
  const StackTrace* top() const;

  /**
   * Return the stack trace following p, or nullptr if p is the bottom of
   * the stack.
   */
  StackTrace* next(StackTrace* p);
  const StackTrace* next(const StackTrace* p) const;

 private:
  Node* state_ = nullptr;
};
} // namespace exception_tracer
} // namespace folly
