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

#include <folly/experimental/symbolizer/test/SymbolizerTestUtils.h>

// NOTE: To simplify generated DWARF keep #includes to a minimum.

namespace folly {
namespace symbolizer {
namespace test {

bool (*gComparatorGetStackTrace)(void* arg) = nullptr;
void* gComparatorGetStackTraceArg = nullptr;

int testComparator(const void* ap, const void* bp) {
  // This comparator is called for the side effect of capturing the stack trace.
  // The function that calls it: lfind usually lives in a separate library -
  // which exercises cross-ELF file support.
  (*gComparatorGetStackTrace)(gComparatorGetStackTraceArg);

  int a = *static_cast<const int*>(ap);
  int b = *static_cast<const int*>(bp);
  // which returns zero if the key object matches the array member, and nonzero
  // otherwise.
  return (a == b) ? 0 : 1;
  // return a < b ? -1 : a > b ? 1 : 0;
}

int kLineno_lfind = 0;
int kLineno_inlineA_lfind = 0;
int kLineno_inlineB_inlineA_lfind = 0;

// NOTE: inlineLTO_inlineA_lfind is only inlined with LTO/ThinLTO.
void inlineLTO_inlineA_lfind() {
  kLineno_inlineA_lfind = __LINE__ + 1;
  inlineA_lfind();
}

void call_inlineA_lfind() {
  inlineA_lfind();
}

void call_inlineB_inlineA_lfind() {
  inlineB_inlineA_lfind();
}

void call_inlineLTO_inlineA_lfind() {
  inlineLTO_inlineA_lfind();
}

class ClassSameFile {
 public:
  __attribute__((__always_inline__)) void memberInline_inlineA_lfind() const {
    kLineno_inlineA_lfind = __LINE__ + 1;
    inlineA_lfind();
  }

  __attribute__((__always_inline__)) static void
  staticMemberInline_inlineA_lfind() {
    kLineno_inlineA_lfind = __LINE__ + 1;
    inlineA_lfind();
  }

  int dummy() const { return dummy_; }
  int dummy_ = 0;
};

void call_same_file_memberInline_inlineA_lfind() {
  ClassSameFile obj;
  obj.memberInline_inlineA_lfind();
}

void call_same_file_staticMemberInline_inlineA_lfind() {
  ClassSameFile::staticMemberInline_inlineA_lfind();
}

void call_different_file_memberInline_inlineA_lfind() {
  ClassDifferentFile obj;
  obj.memberInline_inlineA_lfind();
}

void call_different_file_staticMemberInline_inlineA_lfind() {
  ClassDifferentFile::staticMemberInline_inlineA_lfind();
}

void lexicalBlock_inlineB_inlineA_lfind() try {
  kLineno_inlineB_inlineA_lfind = __LINE__ + 1;
  inlineB_inlineA_lfind();
} catch (...) {
}

void call_lexicalBlock_inlineB_inlineA_lfind() {
  lexicalBlock_inlineB_inlineA_lfind();
}

} // namespace test
} // namespace symbolizer
} // namespace folly
