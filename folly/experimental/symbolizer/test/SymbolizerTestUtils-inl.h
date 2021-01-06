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

// NOTE: To simplify generated DWARF keep #includes to a minimum.

#pragma once

#include <folly/experimental/symbolizer/test/SymbolizerTestUtils.h>

extern "C" {
// Fwd declare instead of #include <stdlib.h> to minimize generated DWARF.
void qsort(
    void* base,
    unsigned long nmemb,
    unsigned long size,
    int (*compar)(const void*, const void*));
} // "C"

namespace folly {
namespace symbolizer {
namespace test {

/*
 * Put the inline functions definition in a separate -inl.h file to cover test
 * cases that define and declare inline functions in different files.
 */

__attribute__((__always_inline__)) inline void inlineA_qsort() {
  int a[2] = {1, 2};
  // Use qsort, which is in a different library
  kLineno_qsort = __LINE__ + 1;
  qsort(a, 2, sizeof(int), testComparator);
}

__attribute__((__always_inline__)) inline void inlineB_inlineA_qsort() {
  kLineno_inlineA_qsort = __LINE__ + 1;
  inlineA_qsort();
}

__attribute__((__always_inline__)) inline void
ClassDifferentFile::memberInline_inlineA_qsort() const {
  kLineno_inlineA_qsort = __LINE__ + 1;
  inlineA_qsort();
}

/* static */ __attribute__((__always_inline__)) inline void
ClassDifferentFile::staticMemberInline_inlineA_qsort() {
  kLineno_inlineA_qsort = __LINE__ + 1;
  inlineA_qsort();
}

} // namespace test
} // namespace symbolizer
} // namespace folly
