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

// NOTE: To simplify generated DWARF keep #includes to a minimum.

namespace folly {
namespace symbolizer {
namespace test {

// Tests capture a stack trace from @testComparator by calling
// gComparatorGetStackTrace(gComparatorGetStackTraceArg).
extern bool (*gComparatorGetStackTrace)(void* arg);
extern void* gComparatorGetStackTraceArg;
int testComparator(const void* ap, const void* bp);

// Set to the line number in the caller function when calling
// qsort/inlineA_qsort/inlineB_inlineA_qsort.
extern int kLineno_qsort;
extern int kLineno_inlineA_qsort;
extern int kLineno_inlineB_inlineA_qsort;

// Debug Info for inlined functions is emitted in the functions where they're
// inlined in. The SymbolizeTest.o object file has many dependencies and will
// have a huge amount of debug info. To simplify debugging call inlined
// functions through these trampolines so that all debug info worth inspecting
// is emitted in the tiny SymbolizerTestUtils.o
void call_inlineA_qsort();
void call_inlineB_inlineA_qsort();
void call_inlineLTO_inlineA_qsort();
void call_same_file_memberInline_inlineA_qsort();
void call_same_file_staticMemberInline_inlineA_qsort();
void call_different_file_memberInline_inlineA_qsort();
void call_different_file_staticMemberInline_inlineA_qsort();
void call_lexicalBlock_inlineB_inlineA_qsort();

// NOTE: inlineLTO_inlineA_qsort is only inlined with LTO/ThinLTO.
void inlineLTO_inlineA_qsort();

class ClassDifferentFile {
 public:
  __attribute__((__always_inline__)) inline void memberInline_inlineA_qsort()
      const;
  __attribute__((__always_inline__)) inline static void
  staticMemberInline_inlineA_qsort();

  int dummy() const { return dummy_; }
  int dummy_ = 0;
};

} // namespace test
} // namespace symbolizer
} // namespace folly

#include <folly/experimental/symbolizer/test/SymbolizerTestUtils-inl.h>
