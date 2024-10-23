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

#include <folly/debugging/symbolizer/Elf.h>
#include <folly/tracing/StaticTracepoint.h>

#if FOLLY_HAVE_ELF

namespace {

void init() {
  FOLLY_SDT(SdtTest, sdt_test_init_enter);
  // ...
  FOLLY_SDT(SdtTest, sdt_test_init_exit);
}

void fini() {
  FOLLY_SDT(SdtTest, sdt_test_fini_enter);
  // ...
  FOLLY_SDT(SdtTest, sdt_test_fini_exit);
}

bool test(const char* filename) {
  using namespace folly::symbolizer;
  ElfFile elf;
  if (elf.openNoThrow(filename) != 0) {
    return 1;
  }
  return elf.iterateSections([&](const auto& shdr) {
    return strcmp(elf.getSectionName(shdr), ".stapsdt.base") == 0;
  });
}

} // namespace

int main(int /*argc*/, char* argv[]) {
  init();
  fini();
  return test(argv[0]) ? 0 : 1;
}

#else

int main() {
  return 0;
}

#endif
