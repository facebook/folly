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

#include <folly/debugging/symbolizer/Elf.h>
#include <folly/debugging/symbolizer/StackTrace.h>
#include <folly/debugging/symbolizer/Symbolizer.h>
#include <folly/portability/GTest.h>

#include <link.h>
#include <cstring>
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

void skipLEB128(const uint8_t*& p) {
  while (*p & 0x80) {
    p++;
  }
  p++;
}

size_t dwEhPeSize(uint8_t enc) {
  switch (enc & 0x0f) {
    case 0x02: // udata2 / sdata2
    case 0x0a:
      return 2;
    case 0x03: // udata4 / sdata4
    case 0x0b:
      return 4;
    case 0x04: // udata8 / sdata8
    case 0x0c:
      return 8;
    default:
      return 0;
  }
}

// Parse a CIE record from .eh_frame and return the FDE pointer encoding
// byte ('R' augmentation). Returns 0 if no 'R' augmentation is present.
uint8_t parseCieFdeEncoding(const uint8_t* p, size_t len) {
  const uint8_t* end = p + len;

  uint8_t version = *p++;

  const char* augStr = reinterpret_cast<const char*>(p);
  p += std::strlen(augStr) + 1;

  // code_alignment_factor (ULEB128)
  skipLEB128(p);
  // data_alignment_factor (SLEB128 — same byte-level skip)
  skipLEB128(p);
  // return_address_register
  if (version == 1) {
    p++;
  } else {
    skipLEB128(p);
  }

  if (augStr[0] != 'z') {
    return 0;
  }

  // augmentation_data_length (ULEB128)
  skipLEB128(p);

  // Walk augmentation string characters (after 'z') to locate 'R'
  for (const char* c = augStr + 1; *c && p < end; ++c) {
    switch (*c) {
      case 'R':
        return *p;
      case 'P': {
        uint8_t enc = *p++;
        size_t sz = dwEhPeSize(enc);
        if (sz == 0) {
          return 0;
        }
        p += sz;
        break;
      }
      case 'L':
        p++;
        break;
      case 'S':
        break;
      default:
        return 0;
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

TEST(StackTraceLargeTest, EhFrameCieSdata8) {
  ElfFile elf;
  auto res = elf.openNoThrow("/proc/self/exe");
  ASSERT_EQ(res, ElfFile::kSuccess)
      << "Failed to open /proc/self/exe: " << (res.msg ? res.msg : "unknown");

  const auto* shdr = elf.getSectionByName(".eh_frame");
  ASSERT_NE(shdr, nullptr) << ".eh_frame section not found in /proc/self/exe";

  auto body = elf.getSectionBody(*shdr);
  const auto* data = reinterpret_cast<const uint8_t*>(body.data());
  size_t ehFrameSize = body.size();
  ASSERT_GT(ehFrameSize, 0u);

  size_t offset = 0;
  int cieCount = 0;

  while (offset + 4 <= ehFrameSize) {
    const uint8_t* entry = data + offset;

    uint32_t length;
    std::memcpy(&length, entry, 4);
    if (length == 0) {
      break;
    }

    const uint8_t* entryData;
    size_t entrySize;
    if (length == 0xFFFFFFFF) {
      uint64_t extLength;
      std::memcpy(&extLength, entry + 4, 8);
      entryData = entry + 12;
      entrySize = static_cast<size_t>(extLength);
      offset += 12 + entrySize;
    } else {
      entryData = entry + 4;
      entrySize = length;
      offset += 4 + entrySize;
    }

    // CIE_id: 0 for CIE, non-zero offset for FDE
    uint32_t cieId;
    std::memcpy(&cieId, entryData, 4);
    if (cieId != 0) {
      continue;
    }

    uint8_t fdeEnc = parseCieFdeEncoding(entryData + 4, entrySize - 4);
    if (fdeEnc == 0) {
      continue;
    }

    uint8_t dataFormat = fdeEnc & 0x0f;
    EXPECT_EQ(dataFormat, kDwEhPeSdata8)
        << "CIE at .eh_frame offset " << (entry - data)
        << " has FDE pointer encoding 0x" << std::hex
        << static_cast<int>(fdeEnc) << ", expected sdata8 (low nibble 0x0c)";
    cieCount++;
  }

  EXPECT_GT(cieCount, 0) << "No CIEs with 'R' augmentation found in .eh_frame";
  LOG(INFO) << "Verified " << cieCount
            << " CIE(s) in .eh_frame all use sdata8 FDE pointer encoding.";
}

TEST(StackTraceLargeTest, StackTraceWithSdata8) {
  foo1();
}

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
