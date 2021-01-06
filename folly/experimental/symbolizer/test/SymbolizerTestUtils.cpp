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

// NOTE: To simplify generated DWARF keep #includes to a minimum.

namespace folly {
namespace symbolizer {
namespace test {

bool (*gComparatorGetStackTrace)(void* arg) = nullptr;
void* gComparatorGetStackTraceArg = nullptr;

int testComparator(const void* ap, const void* bp) {
  // This comparator is called for the side effect of capturing the stack trace.
  // The function that calls it: qsort usually lives in a separate library -
  // which exercises cross-ELF file support.
  (*gComparatorGetStackTrace)(gComparatorGetStackTraceArg);

  int a = *static_cast<const int*>(ap);
  int b = *static_cast<const int*>(bp);
  return a < b ? -1 : a > b ? 1 : 0;
}

int kLineno_qsort = 0;
int kLineno_inlineA_qsort = 0;
int kLineno_inlineB_inlineA_qsort = 0;

// NOTE: inlineLTO_inlineA_qsort is only inlined with LTO/ThinLTO.
void inlineLTO_inlineA_qsort() {
  kLineno_inlineA_qsort = __LINE__ + 1;
  inlineA_qsort();
}

void call_inlineA_qsort() {
  inlineA_qsort();
}

void call_inlineB_inlineA_qsort() {
  inlineB_inlineA_qsort();
}

void call_inlineLTO_inlineA_qsort() {
  inlineLTO_inlineA_qsort();
}

class ClassSameFile {
 public:
  __attribute__((__always_inline__)) void memberInline_inlineA_qsort() const {
    kLineno_inlineA_qsort = __LINE__ + 1;
    inlineA_qsort();
  }

  __attribute__((__always_inline__)) static void
  staticMemberInline_inlineA_qsort() {
    kLineno_inlineA_qsort = __LINE__ + 1;
    inlineA_qsort();
  }

  int dummy() const { return dummy_; }
  int dummy_ = 0;
};

void call_same_file_memberInline_inlineA_qsort() {
  ClassSameFile obj;
  obj.memberInline_inlineA_qsort();
}

void call_same_file_staticMemberInline_inlineA_qsort() {
  ClassSameFile::staticMemberInline_inlineA_qsort();
}

void call_different_file_memberInline_inlineA_qsort() {
  ClassDifferentFile obj;
  obj.memberInline_inlineA_qsort();
}

void call_different_file_staticMemberInline_inlineA_qsort() {
  ClassDifferentFile::staticMemberInline_inlineA_qsort();
}

void lexicalBlock_inlineB_inlineA_qsort() try {
  kLineno_inlineB_inlineA_qsort = __LINE__ + 1;
  inlineB_inlineA_qsort();
} catch (...) {
}

void call_lexicalBlock_inlineB_inlineA_qsort() {
  lexicalBlock_inlineB_inlineA_qsort();
}

} // namespace test
} // namespace symbolizer
} // namespace folly
