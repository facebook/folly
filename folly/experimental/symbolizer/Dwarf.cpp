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

#include <folly/experimental/symbolizer/Dwarf.h>

#include <array>
#include <type_traits>

#include <folly/Optional.h>
#include <folly/experimental/symbolizer/DwarfImpl.h>
#include <folly/experimental/symbolizer/DwarfSection.h>
#include <folly/lang/SafeAssert.h>
#include <folly/portability/Config.h>

#if FOLLY_HAVE_DWARF && FOLLY_HAVE_ELF

#include <dwarf.h>

namespace folly {
namespace symbolizer {

Dwarf::Dwarf(const ElfFile* elf)
    : defaultDebugSections_{
          .elf = elf,
          .debugAbbrev = getSection(".debug_abbrev"),
          .debugAddr = getSection(".debug_addr"),
          .debugAranges = getSection(".debug_aranges"),
          .debugInfo = getSection(".debug_info"),
          .debugLine = getSection(".debug_line"),
          .debugLineStr = getSection(".debug_line_str"),
          .debugLoclists = getSection(".debug_loclists"),
          .debugRanges = getSection(".debug_ranges"),
          .debugRnglists = getSection(".debug_rnglists"),
          .debugStr = getSection(".debug_str"),
          .debugStrOffsets = getSection(".debug_str_offsets")} {
  // Optional sections:
  //  - defaultDebugSections_.debugAranges: for fast address range lookup.
  //     If missing .debug_info can be used - but it's much slower (linear
  //     scan).
  //  - debugRanges_ (DWARF 4) / debugRnglists_ (DWARF 5): non-contiguous
  //    address ranges of debugging information entries.
  //    Used for inline function address lookup.
  if (defaultDebugSections_.debugInfo.empty() ||
      defaultDebugSections_.debugAbbrev.empty() ||
      defaultDebugSections_.debugLine.empty() ||
      defaultDebugSections_.debugStr.empty()) {
    defaultDebugSections_.elf = nullptr;
  }
}

folly::StringPiece Dwarf::getSection(const char* name) const {
  const ElfShdr* elfSection = defaultDebugSections_.elf->getSectionByName(name);
  if (!elfSection) {
    return {};
  }
#ifdef SHF_COMPRESSED
  if (elfSection->sh_flags & SHF_COMPRESSED) {
    return {};
  }
#endif
  return defaultDebugSections_.elf->getSectionBody(*elfSection);
}

namespace {

// Skip over padding until sp.data() - start is a multiple of alignment
void skipPadding(folly::StringPiece& sp, const char* start, size_t alignment) {
  size_t remainder = (sp.data() - start) % alignment;
  if (remainder) {
    FOLLY_SAFE_CHECK(alignment - remainder <= sp.size(), "invalid padding");
    sp.advance(alignment - remainder);
  }
}

/**
 * Find @address in .debug_aranges and return the offset in
 * .debug_info for compilation unit to which this address belongs.
 */
bool findDebugInfoOffset(
    uintptr_t address, StringPiece aranges, uint64_t& offset) {
  DwarfSection section(aranges);
  folly::StringPiece chunk;
  while (section.next(chunk)) {
    auto version = read<uint16_t>(chunk);
    FOLLY_SAFE_CHECK(version == 2, "invalid aranges version");

    offset = readOffset(chunk, section.is64Bit());
    auto addressSize = read<uint8_t>(chunk);
    FOLLY_SAFE_CHECK(addressSize == sizeof(uintptr_t), "invalid address size");
    auto segmentSize = read<uint8_t>(chunk);
    FOLLY_SAFE_CHECK(segmentSize == 0, "segmented architecture not supported");

    // Padded to a multiple of 2 addresses.
    // Strangely enough, this is the only place in the DWARF spec that requires
    // padding.
    skipPadding(chunk, aranges.data(), 2 * sizeof(uintptr_t));
    for (;;) {
      auto start = read<uintptr_t>(chunk);
      auto length = read<uintptr_t>(chunk);

      if (start == 0 && length == 0) {
        break;
      }

      // Is our address in this range?
      if (address >= start && address < start + length) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

bool Dwarf::findAddress(
    uintptr_t address,
    LocationInfoMode mode,
    LocationInfo& locationInfo,
    folly::Range<SymbolizedFrame*> inlineFrames,
    folly::FunctionRef<void(const folly::StringPiece name)> eachParameterName)
    const {
  if (mode == LocationInfoMode::DISABLED) {
    return false;
  }

  if (!defaultDebugSections_.elf) { // No file.
    return false;
  }

  if (!defaultDebugSections_.debugAranges.empty()) {
    // Fast path: find the right .debug_info entry by looking up the
    // address in .debug_aranges.
    uint64_t offset = 0;
    if (findDebugInfoOffset(
            address, defaultDebugSections_.debugAranges, offset)) {
      // Read compilation unit header from .debug_info
      auto unit = getCompilationUnit(defaultDebugSections_, offset);
      if (unit.unitType != DW_UT_compile && unit.unitType != DW_UT_skeleton) {
        return false;
      }
      DwarfImpl impl(unit);
      return impl.findLocation(
          address,
          mode,
          locationInfo,
          inlineFrames,
          eachParameterName,
          false /*checkAddress*/);
    } else if (mode == LocationInfoMode::FAST) {
      // NOTE: Clang (when using -gdwarf-aranges) doesn't generate entries
      // in .debug_aranges for some functions, but always generates
      // .debug_info entries.  Scanning .debug_info is slow, so fall back to
      // it only if such behavior is requested via LocationInfoMode.
      return false;
    } else {
      FOLLY_SAFE_DCHECK(
          mode == LocationInfoMode::FULL ||
              mode == LocationInfoMode::FULL_WITH_INLINE,
          "unexpected mode");
      // Fall back to the linear scan.
    }
  }

  // Slow path (linear scan): Iterate over all .debug_info entries
  // and look for the address in each compilation unit.
  uint64_t offset = 0;
  while (offset < defaultDebugSections_.debugInfo.size()) {
    auto unit = getCompilationUnit(defaultDebugSections_, offset);
    offset += unit.size;
    if (unit.unitType != DW_UT_compile && unit.unitType != DW_UT_skeleton) {
      continue;
    }
    DwarfImpl impl(unit);
    if (impl.findLocation(
            address,
            mode,
            locationInfo,
            inlineFrames,
            eachParameterName,
            true /*checkAddress*/)) {
      return true;
    }
  }
  return false;
}

} // namespace symbolizer
} // namespace folly

#endif // FOLLY_HAVE_DWARF
