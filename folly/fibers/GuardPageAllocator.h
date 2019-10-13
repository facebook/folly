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

#include <memory>

namespace folly {
namespace fibers {

class StackCacheEntry;

/**
 * Stack allocator that protects an extra memory page after
 * the end of the stack.
 * Will only add extra memory pages up to a certain number of allocations
 * to avoid creating too many memory maps for the process.
 */
class GuardPageAllocator {
 public:
  /**
   * @param guardPagesPerStack  Protect a small number of fiber stacks
   *   with this many guard pages.  If 0, acts as std::allocator.
   */
  explicit GuardPageAllocator(size_t guardPagesPerStack);
  ~GuardPageAllocator();

  /**
   * @return pointer to the bottom of the allocated stack of `size' bytes.
   */
  unsigned char* allocate(size_t size);

  /**
   * Deallocates the previous result of an `allocate(size)' call.
   */
  void deallocate(unsigned char* limit, size_t size);

 private:
  std::unique_ptr<StackCacheEntry> stackCache_;
  std::allocator<unsigned char> fallbackAllocator_;
  size_t guardPagesPerStack_{0};
};
} // namespace fibers
} // namespace folly
