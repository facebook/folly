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

// Test that folly's stack trace functions work correctly when the binary
// uses sdata8 .eh_frame_hdr table encoding. The linker script
// eh_sdata8_large.lds places .eh_frame_hdr at 0x200000000 to force
// DW_EH_PE_sdata8, exercising libunwind's sdata8 binary search path.

#include <folly/debugging/symbolizer/StackTrace.h>
#include <folly/debugging/symbolizer/Symbolizer.h>
#include <folly/portability/GTest.h>

#include <link.h>
#include <glog/logging.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

using namespace folly;
using namespace folly::symbolizer;

namespace {

constexpr uint8_t kDwEhPeSdata8 = 0x0c;
constexpr uint8_t kDwEhPeDatarel = 0x30;
constexpr uint8_t kExpectedTableEnc = kDwEhPeDatarel | kDwEhPeSdata8;

struct EhFrameHdrInfo {
  bool found = false;
  uint8_t tableEnc = 0;
};

int ehFrameHdrCallback(struct dl_phdr_info* info, size_t, void* data) {
  auto* result = static_cast<EhFrameHdrInfo*>(data);
  if (info->dlpi_name[0] != '\0') {
    return 0;
  }
  for (int i = 0; i < info->dlpi_phnum; i++) {
    if (info->dlpi_phdr[i].p_type == PT_GNU_EH_FRAME) {
      const auto* hdr = reinterpret_cast<const uint8_t*>(
          info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
      result->found = true;
      result->tableEnc = hdr[3];
      return 1;
    }
  }
  return 0;
}

FOLLY_NOINLINE void verifyStackTraces() {
  constexpr size_t kMaxAddresses = 100;
  FrameArray<kMaxAddresses> fa;
  CHECK(getStackTrace(fa));

  FrameArray<kMaxAddresses> faSafe;
  CHECK(getStackTraceSafe(faSafe));

  FrameArray<kMaxAddresses> faHeap;
  CHECK(getStackTraceHeap(faHeap));

  CHECK_GT(fa.frameCount, 0);
  CHECK_GT(faSafe.frameCount, 0);
  CHECK_GT(faHeap.frameCount, 0);

  CHECK_EQ(fa.frameCount, faSafe.frameCount);
  CHECK_EQ(fa.frameCount, faHeap.frameCount);

  if (VLOG_IS_ON(1)) {
    Symbolizer symbolizer;
    OStreamSymbolizePrinter printer(std::cerr, SymbolizePrinter::COLOR_IF_TTY);

    symbolizer.symbolize(fa);
    VLOG(1) << "getStackTrace\n";
    printer.println(fa);

    symbolizer.symbolize(faSafe);
    VLOG(1) << "getStackTraceSafe\n";
    printer.println(faSafe);

    symbolizer.symbolize(faHeap);
    VLOG(1) << "getStackTraceHeap\n";
    printer.println(faHeap);
  }

  for (size_t i = 2; i < fa.frameCount; ++i) {
    LOG(INFO) << "i=" << i << " " << std::hex << "0x" << fa.addresses[i]
              << " 0x" << faSafe.addresses[i] << " 0x" << faHeap.addresses[i];
    EXPECT_EQ(fa.addresses[i], faSafe.addresses[i]);
    EXPECT_EQ(fa.addresses[i], faHeap.addresses[i]);
  }
}

FOLLY_NOINLINE void foo1();
FOLLY_NOINLINE void foo2();

void foo1() {
  foo2();
}

void foo2() {
  verifyStackTraces();
}

} // namespace

TEST(StackTraceLargeTest, Sdata8Encoding) {
  EhFrameHdrInfo info;
  dl_iterate_phdr(ehFrameHdrCallback, &info);

  ASSERT_TRUE(info.found)
      << "No .eh_frame_hdr found in main executable. "
      << "The linker script may not be applied correctly.";

  ASSERT_EQ(info.tableEnc, kExpectedTableEnc)
      << "Expected sdata8 table encoding (0x" << std::hex
      << static_cast<int>(kExpectedTableEnc) << ") but got 0x"
      << static_cast<int>(info.tableEnc)
      << ". The linker script eh_sdata8_large.lds may not be working.";

  LOG(INFO) << ".eh_frame_hdr table_enc=0x" << std::hex
            << static_cast<int>(info.tableEnc)
            << " (DW_EH_PE_datarel | DW_EH_PE_sdata8) — confirmed.";
}

TEST(StackTraceLargeTest, StackTraceWithSdata8) {
  foo1();
}

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
