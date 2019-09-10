/*
 * Copyright 2019-present Facebook, Inc.
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

#include <folly/container/Array.h>
#include <folly/experimental/symbolizer/Symbolizer.h>

namespace folly {
namespace symbolizer {
namespace test {

FrameArray<100>* framesToFill{nullptr};

int comparator(const void* ap, const void* bp) {
  getStackTrace(*framesToFill);

  int a = *static_cast<const int*>(ap);
  int b = *static_cast<const int*>(bp);
  return a < b ? -1 : a > b ? 1 : 0;
}

FOLLY_ALWAYS_INLINE void inlineFoo(FrameArray<100>& frames) {
  framesToFill = &frames;
  int a[2] = {1, 2};
  // Use qsort, which is in a different library
  qsort(a, 2, sizeof(int), comparator);
  framesToFill = nullptr;
}

FOLLY_ALWAYS_INLINE void inlineFunctionInSeparateFile(FrameArray<100>& frames) {
  inlineFoo(frames);
}

class ClassWithInlineFunction {
 public:
  FOLLY_ALWAYS_INLINE void inlineFunctionInClass(FrameArray<100>& frames) {
    inlineFoo(frames);
  }
};

} // namespace test
} // namespace symbolizer
} // namespace folly
