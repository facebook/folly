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

// Test that C++ exception handling works correctly when the binary uses
// sdata8 .gcc_except_table type table encoding. The linker script
// gcc_except_table_large.lds places .gcc_except_table at 0x200000000 to
// force type table PC-relative relocations to overflow, triggering lld's
// reverse relaxation to expand sdata4 -> sdata8.
//
// Two variants:
//   - Non-PIC (default): TType encoding 0x1b -> 0x1c (pcrel | sdata4 -> sdata8)
//   - PIC (-fPIC):       TType encoding 0x9b -> 0x9c (indirect | pcrel | sdata4
//   -> sdata8)

#include <folly/debugging/symbolizer/Elf.h>
#include <folly/portability/GTest.h>

#include <cstring>
#include <exception>
#include <glog/logging.h>

#if FOLLY_HAVE_ELF

using namespace folly::symbolizer;

namespace {

constexpr uint8_t kDwEhPeOmit = 0xff;
constexpr uint8_t kDwEhPeSdata4 = 0x0b;
constexpr uint8_t kDwEhPeSdata8 = 0x0c;
constexpr uint8_t kDwEhPeFormatMask = 0x0f;

struct ExceptionA : std::exception {
  const char* what() const noexcept override { return "A"; }
};

struct ExceptionB : std::exception {
  const char* what() const noexcept override { return "B"; }
};

[[noreturn]] FOLLY_NOINLINE void throwA() {
  throw ExceptionA();
}
[[noreturn]] FOLLY_NOINLINE void throwB() {
  throw ExceptionB();
}

size_t dwEhPeSize(uint8_t enc) {
  if (enc == kDwEhPeOmit) {
    return 0;
  }
  switch (enc & kDwEhPeFormatMask) {
    case 0x02:
    case 0x0a:
      return 2;
    case 0x03:
    case kDwEhPeSdata4:
      return 4;
    case 0x04:
    case kDwEhPeSdata8:
      return 8;
    case 0x00:
      return sizeof(void*);
    default:
      return 0;
  }
}

uint64_t readULEB128(const uint8_t* _Nonnull& p, const uint8_t* _Nonnull end) {
  uint64_t result = 0;
  unsigned shift = 0;
  while (p < end) {
    uint8_t b = *p++;
    result |= static_cast<uint64_t>(b & 0x7f) << shift;
    if (!(b & 0x80)) {
      break;
    }
    shift += 7;
  }
  return result;
}

struct LsdaHeader {
  uint8_t ttypeEncoding = kDwEhPeOmit;
  size_t totalSize = 0;
};

// Parse an LSDA header from .gcc_except_table content.
// Returns the TType encoding and total LSDA size (if determinable).
std::optional<LsdaHeader> parseLsdaHeader(
    const uint8_t* _Nonnull data, size_t maxSize) {
  if (maxSize < 2) {
    return std::nullopt;
  }

  const uint8_t* _Nonnull p = data;
  const uint8_t* _Nonnull end = data + maxSize;

  uint8_t lpStartEnc = *p++;
  if (lpStartEnc != kDwEhPeOmit) {
    size_t sz = dwEhPeSize(lpStartEnc);
    if (sz == 0) {
      return std::nullopt;
    }
    p += sz;
    if (p >= end) {
      return std::nullopt;
    }
  }

  LsdaHeader hdr;
  hdr.ttypeEncoding = *p++;

  if (hdr.ttypeEncoding != kDwEhPeOmit) {
    if (p >= end) {
      return std::nullopt;
    }
    uint64_t ttypeBaseOffset = readULEB128(p, end);
    hdr.totalSize = static_cast<size_t>(p - data) + ttypeBaseOffset;
    if (hdr.totalSize > maxSize) {
      return std::nullopt;
    }
  } else {
    if (p >= end) {
      return std::nullopt;
    }
    p++; // call site encoding
    if (p >= end) {
      return std::nullopt;
    }
    uint64_t callSiteLen = readULEB128(p, end);
    hdr.totalSize = static_cast<size_t>(p - data) + callSiteLen;
    if (hdr.totalSize > maxSize) {
      return std::nullopt;
    }
  }

  return hdr;
}

} // namespace

TEST(GccExceptTableLargeTest, SectionAddress) {
  ElfFile elf;
  auto res = elf.openNoThrow("/proc/self/exe");
  ASSERT_EQ(res, ElfFile::kSuccess)
      << "Failed to open /proc/self/exe: " << (res.msg ? res.msg : "unknown");

  const auto* shdr = elf.getSectionByName(".gcc_except_table");
  ASSERT_NE(shdr, nullptr)
      << ".gcc_except_table section not found in /proc/self/exe";
  if (shdr == nullptr) {
    return;
  }

  EXPECT_GE(shdr->sh_addr, 0x200000000ULL)
      << "Expected .gcc_except_table at >= 0x200000000 but got 0x" << std::hex
      << shdr->sh_addr
      << ". The linker script gcc_except_table_large.lds may not be applied.";

  LOG(INFO) << ".gcc_except_table addr=0x" << std::hex << shdr->sh_addr
            << " size=" << std::dec << shdr->sh_size << " — confirmed.";
}

TEST(GccExceptTableLargeTest, Sdata8Encoding) {
  ElfFile elf;
  auto res = elf.openNoThrow("/proc/self/exe");
  ASSERT_EQ(res, ElfFile::kSuccess);

  const auto* shdr = elf.getSectionByName(".gcc_except_table");
  ASSERT_NE(shdr, nullptr);
  if (shdr == nullptr) {
    return;
  }

  auto body = elf.getSectionBody(*shdr);
  const uint8_t* _Nonnull data = reinterpret_cast<const uint8_t*>(body.data());
  size_t size = body.size();
  ASSERT_GT(size, 0u);

  int sdata8Count = 0;
  int sdata4Count = 0;
  size_t offset = 0;

  while (offset + 2 <= size) {
    auto hdr = parseLsdaHeader(data + offset, size - offset);
    if (!hdr || hdr->totalSize == 0) {
      offset += 4;
      continue;
    }

    if (hdr->ttypeEncoding != kDwEhPeOmit) {
      uint8_t dataFormat = hdr->ttypeEncoding & kDwEhPeFormatMask;
      if (dataFormat == kDwEhPeSdata8) {
        sdata8Count++;
      } else if (dataFormat == kDwEhPeSdata4) {
        sdata4Count++;
      }
    }

    size_t nextOffset = offset + hdr->totalSize;
    nextOffset = (nextOffset + 3) & ~static_cast<size_t>(3);
    if (nextOffset <= offset) {
      break;
    }
    offset = nextOffset;
  }

  EXPECT_GT(sdata8Count, 0)
      << "No LSDAs were expanded to sdata8. Reverse relaxation may not be "
      << "working for .gcc_except_table type table entries.";

  LOG(INFO) << "sdata8=" << sdata8Count << " sdata4=" << sdata4Count
            << " (sdata4 LSDAs have no overflowing type table relocations)";
}

TEST(GccExceptTableLargeTest, ExceptionHandling) {
  bool caughtA = false;
  try {
    throwA();
  } catch (const ExceptionA&) {
    caughtA = true;
  } catch (const ExceptionB&) {
    FAIL() << "Caught ExceptionB when ExceptionA was thrown";
  }
  EXPECT_TRUE(caughtA) << "Failed to catch ExceptionA";

  bool caughtB = false;
  try {
    throwB();
  } catch (const ExceptionA&) {
    FAIL() << "Caught ExceptionA when ExceptionB was thrown";
  } catch (const ExceptionB&) {
    caughtB = true;
  }
  EXPECT_TRUE(caughtB) << "Failed to catch ExceptionB";

  LOG(INFO) << "Exception handling with sdata8 type tables works correctly.";
}

#endif // FOLLY_HAVE_ELF
