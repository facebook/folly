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

#include <folly/experimental/symbolizer/SymbolizedFrame.h>
#include <folly/experimental/symbolizer/Symbolizer.h>

namespace folly {
namespace symbolizer {
namespace test {

extern void* framesToFill;

template <size_t kNumFrames = 100>
int comparator(const void* ap, const void* bp) {
  getStackTrace(*static_cast<FrameArray<kNumFrames>*>(framesToFill));

  int a = *static_cast<const int*>(ap);
  int b = *static_cast<const int*>(bp);
  return a < b ? -1 : a > b ? 1 : 0;
}

extern size_t kQsortCallLineNo;
extern size_t kFooCallByStandaloneBarLineNo;
extern size_t kFooCallByStandaloneBazLineNo;
extern size_t kFooCallByClassBarLineNo;
extern size_t kFooCallByClassStaticBarLineNo;
extern size_t kFooCallByClassInDifferentFileBarLineNo;
extern size_t kFooCallByClassInDifferentFileStaticBarLineNo;
extern size_t kInlineBarCallByLexicalBarLineNo;

template <size_t kNumFrames = 100>
FOLLY_ALWAYS_INLINE void inlineFoo(FrameArray<kNumFrames>& frames);

template <size_t kNumFrames = 100>
FOLLY_ALWAYS_INLINE void inlineBar(FrameArray<kNumFrames>& frames);

extern void inlineBaz(FrameArray<100>& frames);

class InlineFunctionsWrapper {
 public:
  FOLLY_ALWAYS_INLINE void inlineBar(FrameArray<100>& frames) const;

  FOLLY_ALWAYS_INLINE static void staticInlineBar(FrameArray<100>& frames);

  // Dummy non-inline function.
  size_t dummy() const {
    return dummy_;
  }

  size_t dummy_ = 0;
};

} // namespace test
} // namespace symbolizer
} // namespace folly

#include <folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h>
