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

#include <folly/experimental/symbolizer/test/SymbolizerTestUtils.h>

namespace folly {
namespace symbolizer {
namespace test {

/*
 * Put the inline functions definition in a separate -inl.h file to cover test
 * cases that define and declare inline functions in different files.
 */

template <size_t kNumFrames>
FOLLY_ALWAYS_INLINE void inlineFoo(FrameArray<kNumFrames>& frames) {
  framesToFill = &frames;
  std::array<int, 2> a = {1, 2};
  // Use qsort, which is in a different library
  kQsortCallLineNo = __LINE__ + 1;
  qsort(a.data(), 2, sizeof(int), comparator<kNumFrames>);
  framesToFill = nullptr;
}

template <size_t kNumFrames>
FOLLY_ALWAYS_INLINE void inlineBar(FrameArray<kNumFrames>& frames) {
  kFooCallByStandaloneBarLineNo = __LINE__ + 1;
  inlineFoo(frames);
}

FOLLY_ALWAYS_INLINE void InlineFunctionsWrapper::inlineBar(
    FrameArray<100>& frames) const {
  kFooCallByClassInDifferentFileBarLineNo = __LINE__ + 1;
  inlineFoo(frames);
}

/* static */ FOLLY_ALWAYS_INLINE void InlineFunctionsWrapper::staticInlineBar(
    FrameArray<100>& frames) {
  kFooCallByClassInDifferentFileStaticBarLineNo = __LINE__ + 1;
  inlineFoo(frames);
}

} // namespace test
} // namespace symbolizer
} // namespace folly
