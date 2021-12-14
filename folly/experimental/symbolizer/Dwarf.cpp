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

#include <folly/experimental/symbolizer/Dwarf.h>

#include <array>
#include <type_traits>

#include <folly/Optional.h>
#include <folly/portability/Config.h>

#if FOLLY_HAVE_DWARF && FOLLY_HAVE_ELF

#include <dwarf.h>

// We need a single dwarf5 tag, but may not be building against
// a new enough libdwarf, so just define it ourselves.
#ifndef DW_TAG_skeleton_unit
#define DW_TAG_skeleton_unit 0x4a
#endif

namespace folly {
namespace symbolizer {

namespace detail {

// Abbreviation for a Debugging Information Entry.
struct DIEAbbreviation {
  uint64_t code = 0;
  uint64_t tag = 0;
  bool hasChildren = false;
  folly::StringPiece attributes;
};

struct CompilationUnit {
  bool is64Bit = false;
  uint8_t version = 0;
  uint8_t unitType = DW_UT_compile; // DW_UT_compile or DW_UT_skeleton
  uint8_t addrSize = 0;
  // Offset in .debug_info of this compilation unit.
  uint32_t offset = 0;
  uint32_t size = 0;
  // Offset in .debug_info for the first DIE in this compilation unit.
  uint32_t firstDie = 0;
  uint64_t abbrevOffset = 0;

  // The beginning of the CU's contribution to .debug_addr
  folly::Optional<uint64_t> addrBase; // DW_AT_addr_base (DWARF 5)
  // The beginning of the offsets table (immediately following the
  // header) of the CU's contribution to .debug_loclists
  folly::Optional<uint64_t> loclistsBase; // DW_AT_loclists_base (DWARF 5)
  // The beginning of the offsets table (immediately following the
  // header) of the CU's contribution to .debug_rnglists
  folly::Optional<uint64_t> rnglistsBase; // DW_AT_rnglists_base (DWARF 5)
  // Points to the first string offset of the compilation unit’s
  // contribution to the .debug_str_offsets (or .debug_str_offsets.dwo) section.
  folly::Optional<uint64_t> strOffsetsBase; // DW_AT_str_offsets_base (DWARF 5)

  // Only the CompilationUnit that contains the caller functions needs this.
  // Indexed by (abbr.code - 1) if (abbr.code - 1) < abbrCache.size();
  folly::Range<DIEAbbreviation*> abbrCache;
};

struct Die {
  bool is64Bit = false;
  // Offset from start to first attribute
  uint8_t attrOffset = 0;
  // Offset within debug info.
  uint32_t offset = 0;
  uint64_t code = 0;
  DIEAbbreviation abbr;
};

struct AttributeSpec {
  uint64_t name = 0;
  uint64_t form = 0;
  int64_t implicitConst = 0; // only set when form=DW_FORM_implicit_const

  explicit operator bool() const { return name != 0 || form != 0; }
};

struct Attribute {
  AttributeSpec spec;
  const Die& die;
  boost::variant<uint64_t, folly::StringPiece> attrValue;
};

// Indicates inline funtion `name` is called  at `line@file`.
struct CallLocation {
  Path file = {};
  uint64_t line = 0;
  folly::StringPiece name;
};

} // namespace detail

namespace {

// Maximum number of DIEAbbreviation to cache in a compilation unit. Used to
// speed up inline function lookup.
const uint32_t kMaxAbbreviationEntries = 1000;

// All following read* functions read from a StringPiece, advancing the
// StringPiece, and aborting if there's not enough room.

// Read (bitwise) one object of type T
template <class T>
typename std::enable_if<std::is_pod<T>::value, T>::type read(
    folly::StringPiece& sp) {
  FOLLY_SAFE_CHECK(sp.size() >= sizeof(T), "underflow");
  T x;
  memcpy(&x, sp.data(), sizeof(T));
  sp.advance(sizeof(T));
  return x;
}

// Read (bitwise) an unsigned number of N bytes (N in 1, 2, 3, 4).
template <size_t N>
uint64_t readU64(folly::StringPiece& sp) {
  FOLLY_SAFE_CHECK(sp.size() >= N, "underflow");
  uint64_t x = 0;
  memcpy(&x, sp.data(), N);
  sp.advance(N);
  return x;
}

// Read ULEB (unsigned) varint value; algorithm from the DWARF spec
uint64_t readULEB(folly::StringPiece& sp, uint8_t& shift, uint8_t& val) {
  uint64_t r = 0;
  shift = 0;
  do {
    val = read<uint8_t>(sp);
    r |= ((uint64_t)(val & 0x7f) << shift);
    shift += 7;
  } while (val & 0x80);
  return r;
}

uint64_t readULEB(folly::StringPiece& sp) {
  uint8_t shift;
  uint8_t val;
  return readULEB(sp, shift, val);
}

// Read SLEB (signed) varint value; algorithm from the DWARF spec
int64_t readSLEB(folly::StringPiece& sp) {
  uint8_t shift;
  uint8_t val;
  uint64_t r = readULEB(sp, shift, val);

  if (shift < 64 && (val & 0x40)) {
    r |= -(1ULL << shift); // sign extend
  }

  return r;
}

// Read a value of "section offset" type, which may be 4 or 8 bytes
uint64_t readOffset(folly::StringPiece& sp, bool is64Bit) {
  return is64Bit ? read<uint64_t>(sp) : read<uint32_t>(sp);
}

// Read "len" bytes
folly::StringPiece readBytes(folly::StringPiece& sp, uint64_t len) {
  FOLLY_SAFE_CHECK(len <= sp.size(), "invalid string length");
  folly::StringPiece ret(sp.data(), len);
  sp.advance(len);
  return ret;
}

// Read a null-terminated string
folly::StringPiece readNullTerminated(folly::StringPiece& sp) {
  const char* p = static_cast<const char*>(memchr(sp.data(), 0, sp.size()));
  FOLLY_SAFE_CHECK(p, "invalid null-terminated string");
  folly::StringPiece ret(sp.data(), p);
  sp.assign(p + 1, sp.end());
  return ret;
}

// Skip over padding until sp.data() - start is a multiple of alignment
void skipPadding(folly::StringPiece& sp, const char* start, size_t alignment) {
  size_t remainder = (sp.data() - start) % alignment;
  if (remainder) {
    FOLLY_SAFE_CHECK(alignment - remainder <= sp.size(), "invalid padding");
    sp.advance(alignment - remainder);
  }
}

detail::AttributeSpec readAttributeSpec(folly::StringPiece& sp) {
  detail::AttributeSpec spec;
  spec.name = readULEB(sp);
  spec.form = readULEB(sp);
  if (spec.form == DW_FORM_implicit_const) {
    spec.implicitConst = readSLEB(sp);
  }
  return spec;
}

// Reads an abbreviation from a StringPiece, return true if at end; advance sp
bool readAbbreviation(
    folly::StringPiece& section, detail::DIEAbbreviation& abbr) {
  // Abbreviation code
  abbr.code = readULEB(section);
  if (abbr.code == 0) {
    return false;
  }

  // Abbreviation tag
  abbr.tag = readULEB(section);

  // does this entry have children?
  abbr.hasChildren = (read<uint8_t>(section) != DW_CHILDREN_no);

  // attributes
  const char* attributeBegin = section.data();
  for (;;) {
    FOLLY_SAFE_CHECK(!section.empty(), "invalid attribute section");
    auto spec = readAttributeSpec(section);
    if (!spec) {
      break;
    }
  }
  abbr.attributes.assign(attributeBegin, section.data());
  return true;
}

folly::StringPiece getStringFromStringSection(
    folly::StringPiece str, uint64_t offset) {
  FOLLY_SAFE_CHECK(offset < str.size(), "invalid string offset");
  str.advance(offset);
  return readNullTerminated(str);
}

void readCompilationUnitAbbrs(
    folly::StringPiece abbrev, detail::CompilationUnit& cu) {
  abbrev.advance(cu.abbrevOffset);

  detail::DIEAbbreviation abbr;
  while (readAbbreviation(abbrev, abbr)) {
    // Abbreviation code 0 is reserved for null debugging information entries.
    if (abbr.code != 0 && abbr.code <= kMaxAbbreviationEntries) {
      cu.abbrCache.data()[abbr.code - 1] = abbr;
    }
  }
}

} // namespace

Dwarf::Dwarf(const ElfFile* elf)
    : elf_(elf),
      debugAbbrev_(getSection(".debug_abbrev")),
      debugAddr_(getSection(".debug_addr")),
      debugAranges_(getSection(".debug_aranges")),
      debugInfo_(getSection(".debug_info")),
      debugLine_(getSection(".debug_line")),
      debugLineStr_(getSection(".debug_line_str")),
      debugLoclists_(getSection(".debug_loclists")),
      debugRanges_(getSection(".debug_ranges")),
      debugRnglists_(getSection(".debug_rnglists")),
      debugStr_(getSection(".debug_str")),
      debugStrOffsets_(getSection(".debug_str_offsets")) {
  // Optional sections:
  //  - debugAranges_: for fast address range lookup.
  //     If missing .debug_info can be used - but it's much slower (linear
  //     scan).
  //  - debugRanges_ (DWARF 4) / debugRnglists_ (DWARF 5): non-contiguous
  //    address ranges of debugging information entries.
  //    Used for inline function address lookup.
  if (debugInfo_.empty() || debugAbbrev_.empty() || debugLine_.empty() ||
      debugStr_.empty()) {
    elf_ = nullptr;
  }
}

Dwarf::Section::Section(folly::StringPiece d) : is64Bit_(false), data_(d) {}

// Next chunk in section
bool Dwarf::Section::next(folly::StringPiece& chunk) {
  chunk = data_;
  if (chunk.empty()) {
    return false;
  }

  // Initial length is a uint32_t value for a 32-bit section, and
  // a 96-bit value (0xffffffff followed by the 64-bit length) for a 64-bit
  // section.
  auto initialLength = read<uint32_t>(chunk);
  is64Bit_ = (initialLength == uint32_t(-1));
  auto length = is64Bit_ ? read<uint64_t>(chunk) : initialLength;
  FOLLY_SAFE_CHECK(length <= chunk.size(), "invalid DWARF section");
  chunk.reset(chunk.data(), length);
  data_.assign(chunk.end(), data_.end());
  return true;
}

folly::StringPiece Dwarf::getSection(const char* name) const {
  const ElfShdr* elfSection = elf_->getSectionByName(name);
  if (!elfSection) {
    return {};
  }
#ifdef SHF_COMPRESSED
  if (elfSection->sh_flags & SHF_COMPRESSED) {
    return {};
  }
#endif
  return elf_->getSectionBody(*elfSection);
}

detail::CompilationUnit Dwarf::getCompilationUnit(uint64_t offset) const {
  FOLLY_SAFE_DCHECK(offset < debugInfo_.size(), "unexpected offset");
  detail::CompilationUnit cu;
  folly::StringPiece chunk(debugInfo_);
  cu.offset = offset;
  chunk.advance(offset);

  // 1) unit_length
  auto initialLength = read<uint32_t>(chunk);
  cu.is64Bit = (initialLength == uint32_t(-1));
  cu.size = cu.is64Bit ? read<uint64_t>(chunk) : initialLength;
  FOLLY_SAFE_CHECK(cu.size <= chunk.size(), "invalid chunk size");
  cu.size += cu.is64Bit ? 12 : 4;

  // 2) version
  cu.version = read<uint16_t>(chunk);
  FOLLY_SAFE_CHECK(cu.version >= 2 && cu.version <= 5, "invalid info version");

  if (cu.version == 5) {
    // DWARF5: 7.5.1.1 Full and Partial Compilation Unit Headers
    // 3) unit_type (new DWARF 5)
    cu.unitType = read<uint8_t>(chunk);
    if (cu.unitType != DW_UT_compile && cu.unitType != DW_UT_skeleton) {
      return cu;
    }
    // 4) address_size
    cu.addrSize = read<uint8_t>(chunk);
    FOLLY_SAFE_CHECK(cu.addrSize == sizeof(uintptr_t), "invalid address size");

    // 5) debug_abbrev_offset
    cu.abbrevOffset = readOffset(chunk, cu.is64Bit);

    if (cu.unitType == DW_UT_skeleton) {
      // 6) dwo_id
      read<uint64_t>(chunk);
    }
  } else {
    // DWARF4 has a single type of unit in .debug_info
    cu.unitType = DW_UT_compile;
    // 3) debug_abbrev_offset
    cu.abbrevOffset = readOffset(chunk, cu.is64Bit);
    // 4) address_size
    cu.addrSize = read<uint8_t>(chunk);
    FOLLY_SAFE_CHECK(cu.addrSize == sizeof(uintptr_t), "invalid address size");
  }
  cu.firstDie = chunk.data() - debugInfo_.data();
  if (cu.version < 5) {
    return cu;
  }

  detail::Die die = getDieAtOffset(cu, cu.firstDie);
  if (die.abbr.tag != DW_TAG_compile_unit &&
      die.abbr.tag != DW_TAG_skeleton_unit) {
    return cu;
  }

  // Read the DW_AT_*_base attributes.
  // Attributes which use FORMs relative to these base attrs
  // will not have valid values during this first pass!
  forEachAttribute(cu, die, [&](const detail::Attribute& attr) {
    switch (attr.spec.name) {
      case DW_AT_addr_base:
      case DW_AT_GNU_addr_base:
        cu.addrBase = boost::get<uint64_t>(attr.attrValue);
        break;
      case DW_AT_loclists_base:
        cu.loclistsBase = boost::get<uint64_t>(attr.attrValue);
        break;
      case DW_AT_rnglists_base:
      case DW_AT_GNU_ranges_base:
        cu.rnglistsBase = boost::get<uint64_t>(attr.attrValue);
        break;
      case DW_AT_str_offsets_base:
        cu.strOffsetsBase = boost::get<uint64_t>(attr.attrValue);
        break;
    }
    return true; // continue forEachAttribute
  });
  return cu;
}

// Finds the Compilation Unit starting at offset.
detail::CompilationUnit Dwarf::findCompilationUnit(
    uint64_t targetOffset) const {
  FOLLY_SAFE_DCHECK(
      targetOffset < debugInfo_.size(), "unexpected target address");
  uint64_t offset = 0;
  while (offset < debugInfo_.size()) {
    folly::StringPiece chunk(debugInfo_);
    chunk.advance(offset);

    auto initialLength = read<uint32_t>(chunk);
    auto is64Bit = (initialLength == (uint32_t)-1);
    auto size = is64Bit ? read<uint64_t>(chunk) : initialLength;
    FOLLY_SAFE_CHECK(size <= chunk.size(), "invalid chunk size");
    size += is64Bit ? 12 : 4;

    if (offset + size > targetOffset) {
      break;
    }
    offset += size;
  }
  return getCompilationUnit(offset);
}

detail::DIEAbbreviation Dwarf::getAbbreviation(
    uint64_t code, uint64_t offset) const {
  // Linear search in the .debug_abbrev section, starting at offset
  folly::StringPiece section = debugAbbrev_;
  section.advance(offset);

  detail::DIEAbbreviation abbr;
  while (readAbbreviation(section, abbr)) {
    if (abbr.code == code) {
      return abbr;
    }
  }

  FOLLY_SAFE_CHECK(false, "could not find abbreviation code");
}

/**
 * Find @address in .debug_aranges and return the offset in
 * .debug_info for compilation unit to which this address belongs.
 */
bool Dwarf::findDebugInfoOffset(
    uintptr_t address, StringPiece aranges, uint64_t& offset) {
  Section arangesSection(aranges);
  folly::StringPiece chunk;
  while (arangesSection.next(chunk)) {
    auto version = read<uint16_t>(chunk);
    FOLLY_SAFE_CHECK(version == 2, "invalid aranges version");

    offset = readOffset(chunk, arangesSection.is64Bit());
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

/**
 * Find the @locationInfo for @address in the compilation unit @cu.
 *
 * Best effort:
 * - fills @inlineFrames if mode == FULL_WITH_INLINE,
 * - calls @eachParameterName on the function parameters.
 *
 * if @checkAddress is true, we verify that the address is mapped to
 * a range in this CU before running the line number VM
 */
bool Dwarf::findLocation(
    uintptr_t address,
    const LocationInfoMode mode,
    detail::CompilationUnit& cu,
    LocationInfo& locationInfo,
    folly::Range<SymbolizedFrame*> inlineFrames,
    folly::FunctionRef<void(folly::StringPiece)> eachParameterName,
    bool checkAddress) const {
  detail::Die die = getDieAtOffset(cu, cu.firstDie);
  // Partial compilation unit (DW_TAG_partial_unit) is not supported.
  FOLLY_SAFE_CHECK(
      die.abbr.tag == DW_TAG_compile_unit ||
          die.abbr.tag == DW_TAG_skeleton_unit,
      die.abbr.tag);

  // Offset in .debug_line for the line number VM program for this CU
  folly::Optional<uint64_t> lineOffset;
  folly::StringPiece compilationDirectory;
  folly::Optional<folly::StringPiece> mainFileName;
  folly::Optional<uint64_t> baseAddrCU;
  folly::Optional<uint64_t> rangesOffset;
  bool seenLowPC = false;
  bool seenHighPC = false;
  enum : unsigned {
    kStmtList = 1U << 0,
    kCompDir = 1U << 1,
    kName = 1U << 2,
    kLowPC = 1U << 3,
    kHighPCOrRanges = 1U << 4,
  };
  unsigned expectedAttributes = kStmtList | kCompDir | kName | kLowPC;
  bool foundAddress = !checkAddress;
  if (!foundAddress) {
    expectedAttributes |= kHighPCOrRanges;
  }
  forEachAttribute(cu, die, [&](const detail::Attribute& attr) {
    switch (attr.spec.name) {
      case DW_AT_stmt_list:
        expectedAttributes &= ~kStmtList;
        // Offset in .debug_line for the line number VM program for this
        // compilation unit
        lineOffset = boost::get<uint64_t>(attr.attrValue);
        break;
      case DW_AT_comp_dir:
        expectedAttributes &= ~kCompDir;
        // Compilation directory
        compilationDirectory = boost::get<folly::StringPiece>(attr.attrValue);
        break;
      case DW_AT_name:
        expectedAttributes &= ~kName;
        // File name of main file being compiled
        mainFileName = boost::get<folly::StringPiece>(attr.attrValue);
        break;
      case DW_AT_low_pc:
        expectedAttributes &= ~kLowPC;
        baseAddrCU = boost::get<uint64_t>(attr.attrValue);
        if (!foundAddress) {
          if (address < *baseAddrCU) {
            return false;
          }
          seenLowPC = true;
          if (seenHighPC) {
            foundAddress = true;
          } else if (rangesOffset) {
            if (!isAddrInRangeList(
                    cu, address, baseAddrCU, *rangesOffset, cu.addrSize)) {
              return false;
            }
            foundAddress = true;
          }
        }
        break;
      case DW_AT_high_pc:
        expectedAttributes &= ~kHighPCOrRanges;
        if (!foundAddress) {
          if (address >= boost::get<uint64_t>(attr.attrValue)) {
            return false;
          }
          seenHighPC = true;
          foundAddress = seenLowPC;
        }
        break;
      case DW_AT_ranges:
        // 3.1.1: CU entries have:
        // - either DW_AT_low_pc and DW_AT_high_pc
        // OR
        // - DW_AT_ranges and optional DW_AT_low_pc
        expectedAttributes &= ~kHighPCOrRanges;
        if (!foundAddress) {
          rangesOffset = boost::get<uint64_t>(attr.attrValue);
          if (seenLowPC) {
            if (!isAddrInRangeList(
                    cu, address, baseAddrCU, *rangesOffset, cu.addrSize)) {
              return false;
            }
            foundAddress = true;
          }
        }
        break;
    }
    return (expectedAttributes != 0); // continue forEachAttribute
  });

  if (!foundAddress || !lineOffset) {
    return false;
  }

  if (mainFileName) {
    locationInfo.hasMainFile = true;
    locationInfo.mainFile = Path(compilationDirectory, "", *mainFileName);
  }

  folly::StringPiece lineSection(debugLine_);
  lineSection.advance(*lineOffset);
  LineNumberVM lineVM(
      lineSection, compilationDirectory, debugStr_, debugLineStr_);

  // Execute line number VM program to find file and line
  locationInfo.hasFileAndLine =
      lineVM.findAddress(address, locationInfo.file, locationInfo.line);
  if (!locationInfo.hasFileAndLine) {
    return false;
  }

  // NOTE: locationInfo was found, so findLocation returns success bellow.
  // Missing inline function / parameter name is not a failure (best effort).
  bool checkInline =
      (mode == LocationInfoMode::FULL_WITH_INLINE && !inlineFrames.empty());
  if (!checkInline && !eachParameterName) {
    return true;
  }

  // Re-get the compilation unit with abbreviation cached.
  std::array<detail::DIEAbbreviation, kMaxAbbreviationEntries> abbrs;
  cu.abbrCache = folly::range(abbrs);
  readCompilationUnitAbbrs(debugAbbrev_, cu);

  // Find the subprogram that matches the given address.
  detail::Die subprogram;
  if (!findSubProgramDieForAddress(cu, die, address, baseAddrCU, subprogram)) {
    // Even though @cu contains @address, it's possible
    // that the corresponding DW_TAG_subprogram DIE is missing.
    return true;
  }

  if (eachParameterName) {
    forEachChild(cu, subprogram, [&](const detail::Die& child) {
      if (child.abbr.tag == DW_TAG_formal_parameter) {
        if (auto name =
                getAttribute<folly::StringPiece>(cu, child, DW_AT_name)) {
          eachParameterName(*name);
        }
      }
      return true; // continue forEachChild
    });
  }

  if (!checkInline || !subprogram.abbr.hasChildren) {
    return true;
  }

  // NOTE: @subprogram is the DIE of caller function.

  // Use an extra location and get its call file and call line, so that
  // they can be used for the second last location when we don't have
  // enough inline frames for all inline functions call stack.
  size_t size =
      std::min<size_t>(
          Dwarf::kMaxInlineLocationInfoPerFrame, inlineFrames.size()) +
      1;
  detail::CallLocation callLocations[Dwarf::kMaxInlineLocationInfoPerFrame + 1];
  size_t numFound = 0;
  findInlinedSubroutineDieForAddress(
      cu,
      subprogram,
      lineVM,
      address,
      baseAddrCU,
      folly::Range<detail::CallLocation*>(callLocations, size),
      numFound);
  if (numFound == 0) {
    return true;
  }

  folly::Range<detail::CallLocation*> inlineLocations(callLocations, numFound);
  const auto innerMostFile = locationInfo.file;
  const auto innerMostLine = locationInfo.line;

  // Earlier we filled in locationInfo:
  // - mainFile: the path to the CU -- the file where the non-inlined
  //   call is made from.
  // - file + line: the location of the inner-most inlined call.
  // Here we already find inlined info so mainFile would be redundant.
  locationInfo.hasMainFile = false;
  locationInfo.mainFile = Path{};
  // @findInlinedSubroutineDieForAddress fills inlineLocations[0] with the
  // file+line of the non-inlined outer function making the call.
  // locationInfo.name is already set by the caller by looking up the
  // non-inlined function @address belongs to.
  locationInfo.hasFileAndLine = true;
  locationInfo.file = inlineLocations[0].file;
  locationInfo.line = inlineLocations[0].line;

  // The next inlined subroutine's call file and call line is the current
  // caller's location.
  for (size_t i = 0; i < numFound - 1; i++) {
    inlineLocations[i].file = inlineLocations[i + 1].file;
    inlineLocations[i].line = inlineLocations[i + 1].line;
  }
  // CallLocation for the inner-most inlined function:
  // - will be computed if enough space was available in the passed
  //   buffer.
  // - will have a .name, but no !.file && !.line
  // - its corresponding file+line is the one returned by LineVM based
  //   on @address.
  // Use the inner-most inlined file+line info we got from the LineVM.
  inlineLocations[numFound - 1].file = innerMostFile;
  inlineLocations[numFound - 1].line = innerMostLine;

  // Skip the extra location when actual inline function calls are more
  // than provided frames.
  inlineLocations =
      inlineLocations.subpiece(0, std::min(numFound, inlineFrames.size()));

  // Fill in inline frames in reverse order (as
  // expected by the caller).
  std::reverse(inlineLocations.begin(), inlineLocations.end());
  for (size_t i = 0; i < inlineLocations.size(); i++) {
    inlineFrames[i].found = true;
    inlineFrames[i].addr = address;
    inlineFrames[i].name = inlineLocations[i].name.data();
    inlineFrames[i].location.hasFileAndLine = true;
    inlineFrames[i].location.file = inlineLocations[i].file;
    inlineFrames[i].location.line = inlineLocations[i].line;
  }
  return true;
}

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

  if (!elf_) { // No file.
    return false;
  }

  if (!debugAranges_.empty()) {
    // Fast path: find the right .debug_info entry by looking up the
    // address in .debug_aranges.
    uint64_t offset = 0;
    if (findDebugInfoOffset(address, debugAranges_, offset)) {
      // Read compilation unit header from .debug_info
      auto unit = getCompilationUnit(offset);
      if (unit.unitType != DW_UT_compile && unit.unitType != DW_UT_skeleton) {
        return false;
      }
      return findLocation(
          address,
          mode,
          unit,
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
  while (offset < debugInfo_.size()) {
    auto unit = getCompilationUnit(offset);
    offset += unit.size;
    if (unit.unitType != DW_UT_compile && unit.unitType != DW_UT_skeleton) {
      continue;
    }
    if (findLocation(
            address,
            mode,
            unit,
            locationInfo,
            inlineFrames,
            eachParameterName,
            true /*checkAddress*/)) {
      return true;
    }
  }
  return false;
}

detail::Die Dwarf::getDieAtOffset(
    const detail::CompilationUnit& cu, uint64_t offset) const {
  FOLLY_SAFE_DCHECK(offset < debugInfo_.size(), "unexpected offset");
  detail::Die die;
  folly::StringPiece sp = folly::StringPiece{
      debugInfo_.data() + offset, debugInfo_.data() + cu.offset + cu.size};
  die.offset = offset;
  die.is64Bit = cu.is64Bit;
  auto code = readULEB(sp);
  die.code = code;
  if (code == 0) {
    return die;
  }
  die.attrOffset = sp.data() - debugInfo_.data() - offset;
  die.abbr = !cu.abbrCache.empty() && die.code < kMaxAbbreviationEntries
      ? cu.abbrCache[die.code - 1]
      : getAbbreviation(die.code, cu.abbrevOffset);

  return die;
}

detail::Die Dwarf::findDefinitionDie(
    const detail::CompilationUnit& cu, const detail::Die& die) const {
  // Find the real definition instead of declaration.
  // DW_AT_specification: Incomplete, non-defining, or separate declaration
  // corresponding to a declaration
  auto offset = getAttribute<uint64_t>(cu, die, DW_AT_specification);
  if (!offset) {
    return die;
  }
  return getDieAtOffset(cu, cu.offset + offset.value());
}

size_t Dwarf::forEachChild(
    const detail::CompilationUnit& cu,
    const detail::Die& die,
    folly::FunctionRef<bool(const detail::Die& die)> f) const {
  size_t nextDieOffset =
      forEachAttribute(cu, die, [&](const detail::Attribute&) { return true; });
  if (!die.abbr.hasChildren) {
    return nextDieOffset;
  }

  auto childDie = getDieAtOffset(cu, nextDieOffset);
  while (childDie.code != 0) {
    if (!f(childDie)) {
      return childDie.offset;
    }

    // NOTE: Don't run `f` over grandchildren, just skip over them.
    size_t siblingOffset =
        forEachChild(cu, childDie, [](const detail::Die&) { return true; });
    childDie = getDieAtOffset(cu, siblingOffset);
  }

  // childDie is now a dummy die whose offset is to the code 0 marking the
  // end of the children. Need to add one to get the offset of the next die.
  return childDie.offset + 1;
}

detail::Attribute Dwarf::readAttribute(
    const detail::CompilationUnit& cu,
    const detail::Die& die,
    detail::AttributeSpec spec,
    folly::StringPiece& info) const {
  // DWARF 5 introduces new FORMs whose values are relative to some base attrs:
  // DW_AT_str_offsets_base, DW_AT_rnglists_base, DW_AT_addr_base.
  // Debug Fission DWARF 4 uses GNU DW_AT_GNU_ranges_base & DW_AT_GNU_addr_base.
  //
  // The order in which attributes appear in a CU is not defined.
  // The DW_AT_*_base attrs may appear after attributes that need them.
  // The DW_AT_*_base attrs are CU specific; so we read them just after
  // reading the CU header. During this first pass return empty values
  // when encountering a FORM that depends on DW_AT_*_base.
  auto getStringUsingOffsetTable = [&](uint64_t index) {
    if (!cu.strOffsetsBase.has_value()) {
      return folly::StringPiece();
    }
    // DWARF 5: 7.26 String Offsets Table
    // The DW_AT_str_offsets_base attribute points to the first entry following
    // the header. The entries are indexed sequentially from this base entry,
    // starting from 0.
    auto sp = debugStrOffsets_.subpiece(
        *cu.strOffsetsBase +
        index * (cu.is64Bit ? sizeof(uint64_t) : sizeof(uint32_t)));
    uint64_t strOffset = readOffset(sp, cu.is64Bit);
    return getStringFromStringSection(debugStr_, strOffset);
  };

  auto readDebugAddr = [&](uint64_t index) {
    if (!cu.addrBase.has_value()) {
      return uint64_t(0);
    }
    // DWARF 5: 7.27 Address Table
    // The DW_AT_addr_base attribute points to the first entry following the
    // header. The entries are indexed sequentially from this base entry,
    // starting from 0.
    auto sp = debugAddr_.subpiece(*cu.addrBase + index * sizeof(uint64_t));
    return read<uint64_t>(sp);
  };

  switch (spec.form) {
    case DW_FORM_addr:
      return {spec, die, read<uintptr_t>(info)};
    case DW_FORM_block1:
      return {spec, die, readBytes(info, read<uint8_t>(info))};
    case DW_FORM_block2:
      return {spec, die, readBytes(info, read<uint16_t>(info))};
    case DW_FORM_block4:
      return {spec, die, readBytes(info, read<uint32_t>(info))};
    case DW_FORM_block:
      FOLLY_FALLTHROUGH;
    case DW_FORM_exprloc:
      return {spec, die, readBytes(info, readULEB(info))};
    case DW_FORM_data1:
      FOLLY_FALLTHROUGH;
    case DW_FORM_ref1:
      return {spec, die, read<uint8_t>(info)};
    case DW_FORM_data2:
      FOLLY_FALLTHROUGH;
    case DW_FORM_ref2:
      return {spec, die, read<uint16_t>(info)};
    case DW_FORM_data4:
      FOLLY_FALLTHROUGH;
    case DW_FORM_ref4:
      return {spec, die, read<uint32_t>(info)};
    case DW_FORM_data8:
      FOLLY_FALLTHROUGH;
    case DW_FORM_ref8:
      FOLLY_FALLTHROUGH;
    case DW_FORM_ref_sig8:
      return {spec, die, read<uint64_t>(info)};
    case DW_FORM_sdata:
      return {spec, die, readSLEB(info)};
    case DW_FORM_udata:
      FOLLY_FALLTHROUGH;
    case DW_FORM_ref_udata:
      return {spec, die, readULEB(info)};
    case DW_FORM_flag:
      return {spec, die, read<uint8_t>(info)};
    case DW_FORM_flag_present:
      return {spec, die, 1};
    case DW_FORM_sec_offset:
      FOLLY_FALLTHROUGH;
    case DW_FORM_ref_addr:
      return {spec, die, readOffset(info, die.is64Bit)};
    case DW_FORM_string:
      return {spec, die, readNullTerminated(info)};
    case DW_FORM_strp:
      return {
          spec,
          die,
          getStringFromStringSection(debugStr_, readOffset(info, die.is64Bit))};
    case DW_FORM_indirect: // form is explicitly specified
      // Update spec with the actual FORM.
      spec.form = readULEB(info);
      return readAttribute(cu, die, spec, info);

    // DWARF 5:
    case DW_FORM_implicit_const: // form is explicitly specified
      // For attributes with this form, the attribute specification contains a
      // third part, which is a signed LEB128 number. The value of this number
      // is used as the value of the attribute, and no value is stored in the
      // .debug_info section.
      return {spec, die, spec.implicitConst};

    case DW_FORM_addrx:
      return {spec, die, readDebugAddr(readULEB(info))};
    case DW_FORM_addrx1:
      return {spec, die, readDebugAddr(readU64<1>(info))};
    case DW_FORM_addrx2:
      return {spec, die, readDebugAddr(readU64<2>(info))};
    case DW_FORM_addrx3:
      return {spec, die, readDebugAddr(readU64<3>(info))};
    case DW_FORM_addrx4:
      return {spec, die, readDebugAddr(readU64<4>(info))};

    case DW_FORM_line_strp:
      return {
          spec,
          die,
          getStringFromStringSection(
              debugLineStr_, readOffset(info, die.is64Bit))};

    case DW_FORM_strx:
      return {spec, die, getStringUsingOffsetTable(readULEB(info))};
    case DW_FORM_strx1:
      return {spec, die, getStringUsingOffsetTable(readU64<1>(info))};
    case DW_FORM_strx2:
      return {spec, die, getStringUsingOffsetTable(readU64<2>(info))};
    case DW_FORM_strx3:
      return {spec, die, getStringUsingOffsetTable(readU64<3>(info))};
    case DW_FORM_strx4:
      return {spec, die, getStringUsingOffsetTable(readU64<4>(info))};

    case DW_FORM_rnglistx: {
      auto index = readULEB(info);
      if (!cu.rnglistsBase.has_value()) {
        return {spec, die, 0};
      }
      const uint64_t offsetSize =
          cu.is64Bit ? sizeof(uint64_t) : sizeof(uint32_t);
      auto sp = debugRnglists_.subpiece(*cu.rnglistsBase + index * offsetSize);
      auto offset = readOffset(sp, cu.is64Bit);
      return {spec, die, *cu.rnglistsBase + offset};
    } break;

    case DW_FORM_loclistx: {
      auto index = readULEB(info);
      if (!cu.loclistsBase.has_value()) {
        return {spec, die, 0};
      }
      const uint64_t offsetSize =
          cu.is64Bit ? sizeof(uint64_t) : sizeof(uint32_t);
      auto sp = debugLoclists_.subpiece(*cu.loclistsBase + index * offsetSize);
      auto offset = readOffset(sp, cu.is64Bit);
      return {spec, die, *cu.loclistsBase + offset};
    } break;

    case DW_FORM_data16:
      return {spec, die, readBytes(info, 16)};

    case DW_FORM_ref_sup4:
    case DW_FORM_ref_sup8:
    case DW_FORM_strp_sup:
      FOLLY_SAFE_CHECK(
          false, "Unexpected DWARF5 supplimentary object files: ", spec.form);

    default:
      FOLLY_SAFE_CHECK(false, "invalid attribute form: ", spec.form);
  }
  return {spec, die, 0};
}

/*
 * Iterate over all attributes of the given DIE, calling the given callable
 * for each. Iteration is stopped early if any of the calls return false.
 */
size_t Dwarf::forEachAttribute(
    const detail::CompilationUnit& cu,
    const detail::Die& die,
    folly::FunctionRef<bool(const detail::Attribute& die)> f) const {
  auto attrs = die.abbr.attributes;
  auto values = folly::StringPiece{
      debugInfo_.data() + die.offset + die.attrOffset,
      debugInfo_.data() + cu.offset + cu.size};
  while (auto spec = readAttributeSpec(attrs)) {
    auto attr = readAttribute(cu, die, spec, values);
    if (!f(attr)) {
      return static_cast<size_t>(-1);
    }
  }
  return values.data() - debugInfo_.data();
}

template <class T>
folly::Optional<T> Dwarf::getAttribute(
    const detail::CompilationUnit& cu,
    const detail::Die& die,
    uint64_t attrName) const {
  folly::Optional<T> result;
  forEachAttribute(cu, die, [&](const detail::Attribute& attr) {
    if (attr.spec.name == attrName) {
      result = boost::get<T>(attr.attrValue);
      return false;
    }
    return true;
  });
  return result;
}

bool Dwarf::isAddrInRangeList(
    const detail::CompilationUnit& cu,
    uint64_t address,
    folly::Optional<uint64_t> baseAddr,
    size_t offset,
    uint8_t addrSize) const {
  FOLLY_SAFE_CHECK(
      addrSize == 4 || addrSize == 8, "wrong address size: ", int(addrSize));
  if (cu.version <= 4 && !debugRanges_.empty()) {
    const bool is64BitAddr = addrSize == 8;
    folly::StringPiece sp = debugRanges_;
    sp.advance(offset);
    const uint64_t maxAddr = is64BitAddr ? std::numeric_limits<uint64_t>::max()
                                         : std::numeric_limits<uint32_t>::max();
    while (!sp.empty()) {
      uint64_t begin = readOffset(sp, is64BitAddr);
      uint64_t end = readOffset(sp, is64BitAddr);
      // The range list entry is a base address selection entry.
      if (begin == maxAddr) {
        baseAddr = end;
        continue;
      }
      // The range list entry is an end of list entry.
      if (begin == 0 && end == 0) {
        break;
      }

      // Check if the given address falls in the range list entry.
      // 2.17.3 Non-Contiguous Address Ranges
      // The applicable base address of a range list entry is determined by the
      // closest preceding base address selection entry (see below) in the same
      // range list. If there is no such selection entry, then the applicable
      // base address defaults to the base address of the compilation unit.
      if (baseAddr && address >= begin + *baseAddr &&
          address < end + *baseAddr) {
        return true;
      }
    };
  }

  if (cu.version == 5 && !debugRnglists_.empty() && cu.addrBase.has_value()) {
    auto debugRnglists = debugRnglists_;
    debugRnglists.advance(offset);

    while (!debugRnglists.empty()) {
      auto kind = read<uint8_t>(debugRnglists);
      switch (kind) {
        case DW_RLE_end_of_list:
          return false;
        case DW_RLE_base_addressx: {
          auto index = readULEB(debugRnglists);
          auto sp =
              debugAddr_.subpiece(*cu.addrBase + index * sizeof(uint64_t));
          baseAddr = read<uint64_t>(sp);
        } break;

        case DW_RLE_startx_endx: {
          auto indexStart = readULEB(debugRnglists);
          auto indexEnd = readULEB(debugRnglists);
          auto spStart =
              debugAddr_.subpiece(*cu.addrBase + indexStart * sizeof(uint64_t));
          auto start = read<uint64_t>(spStart);

          auto spEnd =
              debugAddr_.subpiece(*cu.addrBase + indexEnd * sizeof(uint64_t));
          auto end = read<uint64_t>(spEnd);
          if (address >= start && address < end) {
            return true;
          }
        } break;

        case DW_RLE_startx_length: {
          auto indexStart = readULEB(debugRnglists);
          auto length = readULEB(debugRnglists);
          auto spStart =
              debugAddr_.subpiece(*cu.addrBase + indexStart * sizeof(uint64_t));
          auto start = read<uint64_t>(spStart);

          auto spEnd = debugAddr_.subpiece(
              *cu.addrBase + indexStart * sizeof(uint64_t) + length);
          auto end = read<uint64_t>(spEnd);
          if (start != end && address >= start && address < end) {
            return true;
          }
        } break;

        case DW_RLE_offset_pair: {
          auto offsetStart = readULEB(debugRnglists);
          auto offsetEnd = readULEB(debugRnglists);
          if (baseAddr && address >= (*baseAddr + offsetStart) &&
              address < (*baseAddr + offsetEnd)) {
            return true;
          }
        } break;

        case DW_RLE_base_address:
          baseAddr = read<uint64_t>(debugRnglists);
          break;

        case DW_RLE_start_end: {
          uint64_t start = read<uint64_t>(debugRnglists);
          uint64_t end = read<uint64_t>(debugRnglists);
          if (address >= start && address < end) {
            return true;
          }
        } break;

        case DW_RLE_start_length: {
          uint64_t start = read<uint64_t>(debugRnglists);
          uint64_t end = start + readULEB(debugRnglists);
          if (address >= start && address < end) {
            return true;
          }
        } break;

        default:
          FOLLY_SAFE_CHECK(
              false,
              "Unexpected debug_rnglists entry kind: ",
              static_cast<int>(kind));
      }
    }
  }
  return false;
}

bool Dwarf::findSubProgramDieForAddress(
    const detail::CompilationUnit& cu,
    const detail::Die& die,
    uint64_t address,
    folly::Optional<uint64_t> baseAddrCU,
    detail::Die& subprogram) const {
  forEachChild(cu, die, [&](const detail::Die& childDie) {
    if (childDie.abbr.tag == DW_TAG_subprogram) {
      folly::Optional<uint64_t> lowPc;
      folly::Optional<uint64_t> highPc;
      folly::Optional<bool> isHighPcAddr;
      folly::Optional<uint64_t> rangeOffset;
      forEachAttribute(cu, childDie, [&](const detail::Attribute& attr) {
        switch (attr.spec.name) {
          case DW_AT_ranges:
            rangeOffset = boost::get<uint64_t>(attr.attrValue);
            break;
          case DW_AT_low_pc:
            lowPc = boost::get<uint64_t>(attr.attrValue);
            break;
          case DW_AT_high_pc:
            // The value of the DW_AT_high_pc attribute can be
            // an address (DW_FORM_addr*) or an offset (DW_FORM_data*).
            isHighPcAddr = attr.spec.form == DW_FORM_addr || //
                attr.spec.form == DW_FORM_addrx || //
                attr.spec.form == DW_FORM_addrx1 || //
                attr.spec.form == DW_FORM_addrx2 || //
                attr.spec.form == DW_FORM_addrx3 || //
                attr.spec.form == DW_FORM_addrx4;
            highPc = boost::get<uint64_t>(attr.attrValue);
            break;
        }
        return true; // continue forEachAttribute
      });

      bool pcMatch = lowPc && highPc && isHighPcAddr && address >= *lowPc &&
          (address < (*isHighPcAddr ? *highPc : *lowPc + *highPc));
      if (pcMatch) {
        subprogram = childDie;
        return false; // stop forEachChild
      }

      bool rangeMatch =
          rangeOffset &&
          isAddrInRangeList(
              cu, address, baseAddrCU, rangeOffset.value(), cu.addrSize);
      if (rangeMatch) {
        subprogram = childDie;
        return false; // stop forEachChild
      }
    }

    // Continue forEachChild to next sibling DIE only if not already found.
    return !findSubProgramDieForAddress(
        cu, childDie, address, baseAddrCU, subprogram);
  });
  return subprogram.abbr.tag == DW_TAG_subprogram;
}

/**
 * Find DW_TAG_inlined_subroutine child DIEs that contain @address and
 * then extract:
 * - Where was it called from (DW_AT_call_file & DW_AT_call_line):
 *   the statement or expression that caused the inline expansion.
 * - The inlined function's name. As a function may be inlined multiple
 *   times, common attributes like DW_AT_linkage_name or DW_AT_name
 *   are only stored in its "concrete out-of-line instance" (a
 *   DW_TAG_subprogram) which we find using DW_AT_abstract_origin.
 */
void Dwarf::findInlinedSubroutineDieForAddress(
    const detail::CompilationUnit& cu,
    const detail::Die& die,
    const LineNumberVM& lineVM,
    uint64_t address,
    folly::Optional<uint64_t> baseAddrCU,
    folly::Range<detail::CallLocation*> locations,
    size_t& numFound) const {
  if (numFound >= locations.size()) {
    return;
  }

  forEachChild(cu, die, [&](const detail::Die& childDie) {
    // Between a DW_TAG_subprogram and and DW_TAG_inlined_subroutine we might
    // have arbitrary intermediary "nodes", including DW_TAG_common_block,
    // DW_TAG_lexical_block, DW_TAG_try_block, DW_TAG_catch_block and
    // DW_TAG_with_stmt, etc.
    // We can't filter with locationhere since its range may be not specified.
    // See section 2.6.2: A location list containing only an end of list entry
    // describes an object that exists in the source code but not in the
    // executable program.
    if (childDie.abbr.tag == DW_TAG_try_block ||
        childDie.abbr.tag == DW_TAG_catch_block ||
        childDie.abbr.tag == DW_TAG_entry_point ||
        childDie.abbr.tag == DW_TAG_common_block ||
        childDie.abbr.tag == DW_TAG_lexical_block) {
      findInlinedSubroutineDieForAddress(
          cu, childDie, lineVM, address, baseAddrCU, locations, numFound);
      return true;
    }

    folly::Optional<uint64_t> lowPc;
    folly::Optional<uint64_t> highPc;
    folly::Optional<bool> isHighPcAddr;
    folly::Optional<uint64_t> abstractOrigin;
    folly::Optional<uint64_t> abstractOriginRefType;
    folly::Optional<uint64_t> callFile;
    folly::Optional<uint64_t> callLine;
    folly::Optional<uint64_t> rangeOffset;
    forEachAttribute(cu, childDie, [&](const detail::Attribute& attr) {
      switch (attr.spec.name) {
        case DW_AT_ranges:
          rangeOffset = boost::get<uint64_t>(attr.attrValue);
          break;
        case DW_AT_low_pc:
          lowPc = boost::get<uint64_t>(attr.attrValue);
          break;
        case DW_AT_high_pc:
          // The value of the DW_AT_high_pc attribute can be
          // an address (DW_FORM_addr*) or an offset (DW_FORM_data*).
          isHighPcAddr = attr.spec.form == DW_FORM_addr || //
              attr.spec.form == DW_FORM_addrx || //
              attr.spec.form == DW_FORM_addrx1 || //
              attr.spec.form == DW_FORM_addrx2 || //
              attr.spec.form == DW_FORM_addrx3 || //
              attr.spec.form == DW_FORM_addrx4;
          highPc = boost::get<uint64_t>(attr.attrValue);
          break;
        case DW_AT_abstract_origin:
          abstractOriginRefType = attr.spec.form;
          abstractOrigin = boost::get<uint64_t>(attr.attrValue);
          break;
        case DW_AT_call_line:
          callLine = boost::get<uint64_t>(attr.attrValue);
          break;
        case DW_AT_call_file:
          callFile = boost::get<uint64_t>(attr.attrValue);
          break;
      }
      return true; // continue forEachAttribute
    });

    // 2.17 Code Addresses and Ranges
    // Any debugging information entry describing an entity that has a
    // machine code address or range of machine code addresses,
    // which includes compilation units, module initialization, subroutines,
    // ordinary blocks, try/catch blocks, labels and the like, may have
    //  - A DW_AT_low_pc attribute for a single address,
    //  - A DW_AT_low_pc and DW_AT_high_pc pair of attributes for a
    //    single contiguous range of addresses, or
    //  - A DW_AT_ranges attribute for a non-contiguous range of addresses.
    // TODO: Support DW_TAG_entry_point and DW_TAG_common_block that don't
    // have DW_AT_low_pc/DW_AT_high_pc pairs and DW_AT_ranges.
    // TODO: Support relocated address which requires lookup in relocation map.
    bool pcMatch = lowPc && highPc && isHighPcAddr && address >= *lowPc &&
        (address < (*isHighPcAddr ? *highPc : (*lowPc + *highPc)));
    bool rangeMatch =
        rangeOffset &&
        isAddrInRangeList(
            cu, address, baseAddrCU, rangeOffset.value(), cu.addrSize);
    if (!pcMatch && !rangeMatch) {
      // Address doesn't match. Keep searching other children.
      return true;
    }

    if (!abstractOrigin || !abstractOriginRefType || !callLine || !callFile) {
      // We expect a single sibling DIE to match on addr, but it's missing
      // required fields. Stop searching for other DIEs.
      return false;
    }

    locations[numFound].file = lineVM.getFullFileName(*callFile);
    locations[numFound].line = *callLine;

    auto getFunctionName = [&](const detail::CompilationUnit& srcu,
                               uint64_t dieOffset) {
      auto declDie = getDieAtOffset(srcu, dieOffset);
      // Jump to the actual function definition instead of declaration for name
      // and line info.
      auto defDie = findDefinitionDie(srcu, declDie);

      folly::StringPiece name;
      // The file and line will be set in the next inline subroutine based on
      // its DW_AT_call_file and DW_AT_call_line.
      forEachAttribute(srcu, defDie, [&](const detail::Attribute& attr) {
        switch (attr.spec.name) {
          case DW_AT_linkage_name:
            name = boost::get<folly::StringPiece>(attr.attrValue);
            break;
          case DW_AT_name:
            // NOTE: when DW_AT_linkage_name and DW_AT_name match, dwarf
            // emitters omit DW_AT_linkage_name (to save space). If present
            // DW_AT_linkage_name should always be preferred (mangled C++ name
            // vs just the function name).
            if (name.empty()) {
              name = boost::get<folly::StringPiece>(attr.attrValue);
            }
            break;
        }
        return true; // continue forEachAttribute
      });
      return name;
    };

    // DW_AT_abstract_origin is a reference. There a 3 types of references:
    // - the reference can identify any debugging information entry within the
    //   compilation unit (DW_FORM_ref1, DW_FORM_ref2, DW_FORM_ref4,
    //   DW_FORM_ref8, DW_FORM_ref_udata). This type of reference is an offset
    //   from the first byte of the compilation header for the compilation unit
    //   containing the reference.
    // - the reference can identify any debugging information entry within a
    //   .debug_info section; in particular, it may refer to an entry in a
    //   different compilation unit (DW_FORM_ref_addr)
    // - the reference can identify any debugging information type entry that
    //   has been placed in its own type unit.
    //   Not applicable for DW_AT_abstract_origin.
    locations[numFound].name = (*abstractOriginRefType != DW_FORM_ref_addr)
        ? getFunctionName(cu, cu.offset + *abstractOrigin)
        : getFunctionName(
              findCompilationUnit(*abstractOrigin), *abstractOrigin);

    findInlinedSubroutineDieForAddress(
        cu, childDie, lineVM, address, baseAddrCU, locations, ++numFound);

    return false;
  });
}

Dwarf::LineNumberVM::LineNumberVM(
    folly::StringPiece data,
    folly::StringPiece compilationDirectory,
    folly::StringPiece debugStr,
    folly::StringPiece debugLineStr)
    : compilationDirectory_(compilationDirectory),
      debugStr_(debugStr),
      debugLineStr_(debugLineStr) {
  Section section(data);
  FOLLY_SAFE_CHECK(section.next(data_), "invalid line number VM");
  is64Bit_ = section.is64Bit();
  init();
  reset();
}

void Dwarf::LineNumberVM::reset() {
  address_ = 0;
  file_ = 1;
  line_ = 1;
  column_ = 0;
  isStmt_ = defaultIsStmt_;
  basicBlock_ = false;
  endSequence_ = false;
  prologueEnd_ = false;
  epilogueBegin_ = false;
  isa_ = 0;
  discriminator_ = 0;
}

struct LineNumberAttribute {
  uint64_t contentTypeCode;
  uint64_t formCode;
  boost::variant<uint64_t, folly::StringPiece> attrValue;
};

LineNumberAttribute readLineNumberAttribute(
    bool is64Bit,
    folly::StringPiece& format,
    folly::StringPiece& entries,
    folly::StringPiece debugStr,
    folly::StringPiece debugLineStr) {
  uint64_t contentTypeCode = readULEB(format);
  uint64_t formCode = readULEB(format);
  boost::variant<uint64_t, folly::StringPiece> attrValue;

  switch (contentTypeCode) {
    case DW_LNCT_path: {
      switch (formCode) {
        case DW_FORM_string:
          attrValue = readNullTerminated(entries);
          break;
        case DW_FORM_line_strp: {
          auto off = readOffset(entries, is64Bit);
          attrValue = getStringFromStringSection(debugLineStr, off);
        } break;
        case DW_FORM_strp:
          attrValue = getStringFromStringSection(
              debugStr, readOffset(entries, is64Bit));
          break;
        case DW_FORM_strp_sup:
          FOLLY_SAFE_CHECK(false, "Unexpected DW_FORM_strp_sup");
          break;
        default:
          FOLLY_SAFE_CHECK(
              false, "Unexpected form for DW_LNCT_path: ", formCode);
          break;
      }
    } break;

    case DW_LNCT_directory_index: {
      switch (formCode) {
        case DW_FORM_data1:
          attrValue = read<uint8_t>(entries);
          break;
        case DW_FORM_data2:
          attrValue = read<uint16_t>(entries);
          break;
        case DW_FORM_udata:
          attrValue = readULEB(entries);
          break;
        default:
          FOLLY_SAFE_CHECK(
              false, "Unexpected form for DW_LNCT_directory_index: ", formCode);
          break;
      }
    } break;

    case DW_LNCT_timestamp: {
      switch (formCode) {
        case DW_FORM_udata:
          attrValue = readULEB(entries);
          break;
        case DW_FORM_data4:
          attrValue = read<uint32_t>(entries);
          break;
        case DW_FORM_data8:
          attrValue = read<uint64_t>(entries);
          break;
        case DW_FORM_block:
          attrValue = readBytes(entries, readULEB(entries));
          break;
        default:
          FOLLY_SAFE_CHECK(
              false, "Unexpected form for DW_LNCT_timestamp: ", formCode);
      }
    } break;

    case DW_LNCT_size: {
      switch (formCode) {
        case DW_FORM_udata:
          attrValue = readULEB(entries);
          break;
        case DW_FORM_data1:
          attrValue = read<uint8_t>(entries);
          break;
        case DW_FORM_data2:
          attrValue = read<uint16_t>(entries);
          break;
        case DW_FORM_data4:
          attrValue = read<uint32_t>(entries);
          break;
        case DW_FORM_data8:
          attrValue = read<uint64_t>(entries);
          break;
        default:
          FOLLY_SAFE_CHECK(
              false, "Unexpected form for DW_LNCT_size: ", formCode);
          break;
      }
    } break;

    case DW_LNCT_MD5: {
      switch (formCode) {
        case DW_FORM_data16:
          attrValue = readBytes(entries, 16);
          break;
        default:
          FOLLY_SAFE_CHECK(
              false, "Unexpected form for DW_LNCT_MD5: ", formCode);
          break;
      }
    } break;

    default:
      // TODO: skip over vendor data as specified by the form instead.
      FOLLY_SAFE_CHECK(
          false, "Unexpected vendor content type code: ", contentTypeCode);
      break;
  }
  return {
      .contentTypeCode = contentTypeCode,
      .formCode = formCode,
      .attrValue = attrValue,
  };
}

void Dwarf::LineNumberVM::init() {
  version_ = read<uint16_t>(data_);
  FOLLY_SAFE_CHECK(
      version_ >= 2 && version_ <= 5,
      "invalid version in line number VM: ",
      version_);
  if (version_ == 5) {
    auto addressSize = read<uint8_t>(data_);
    FOLLY_SAFE_CHECK(
        addressSize == sizeof(uintptr_t),
        "Unexpected Line Number Table address_size: ",
        addressSize);
    auto segment_selector_size = read<uint8_t>(data_);
    FOLLY_SAFE_CHECK(segment_selector_size == 0, "Segments not supported");
  }
  uint64_t headerLength = readOffset(data_, is64Bit_);
  FOLLY_SAFE_CHECK(
      headerLength <= data_.size(),
      "invalid line number VM header length: headerLength: ",
      headerLength,
      " data_.size(): ",
      data_.size());
  folly::StringPiece header(data_.data(), headerLength);
  data_.assign(header.end(), data_.end());

  minLength_ = read<uint8_t>(header);
  if (version_ >= 4) { // Version 2 and 3 records don't have this
    uint8_t maxOpsPerInstruction = read<uint8_t>(header);
    FOLLY_SAFE_CHECK(maxOpsPerInstruction == 1, "VLIW not supported");
  }
  defaultIsStmt_ = read<uint8_t>(header);
  lineBase_ = read<int8_t>(header); // yes, signed
  lineRange_ = read<uint8_t>(header);
  opcodeBase_ = read<uint8_t>(header);
  FOLLY_SAFE_CHECK(opcodeBase_ != 0, "invalid opcode base");
  standardOpcodeLengths_ = reinterpret_cast<const uint8_t*>(header.data());
  header.advance(opcodeBase_ - 1);

  if (version_ <= 4) {
    // We don't want to use heap, so we don't keep an unbounded amount of state.
    // We'll just skip over include directories and file names here, and
    // we'll loop again when we actually need to retrieve one.
    folly::StringPiece sp;
    const char* tmp = header.data();
    v4_.includeDirectoryCount = 0;
    while (!(sp = readNullTerminated(header)).empty()) {
      ++v4_.includeDirectoryCount;
    }
    v4_.includeDirectories.assign(tmp, header.data());

    tmp = header.data();
    FileName fn;
    v4_.fileNameCount = 0;
    while (readFileName(header, fn)) {
      ++v4_.fileNameCount;
    }
    v4_.fileNames.assign(tmp, header.data());
  } else if (version_ == 5) {
    v5_.directoryEntryFormatCount = read<uint8_t>(header);
    const char* tmp = header.data();
    for (uint8_t i = 0; i < v5_.directoryEntryFormatCount; i++) {
      // A sequence of directory entry format descriptions. Each description
      // consists of a pair of ULEB128 values:
      readULEB(header); // A content type code
      readULEB(header); // A form code using the attribute form codes
    }
    v5_.directoryEntryFormat.assign(tmp, header.data());
    v5_.directoriesCount = readULEB(header);
    tmp = header.data();
    for (uint64_t i = 0; i < v5_.directoriesCount; i++) {
      folly::StringPiece format = v5_.directoryEntryFormat;
      for (uint8_t f = 0; f < v5_.directoryEntryFormatCount; f++) {
        readLineNumberAttribute(
            is64Bit_, format, header, debugStr_, debugLineStr_);
      }
    }
    v5_.directories.assign(tmp, header.data());

    v5_.fileNameEntryFormatCount = read<uint8_t>(header);
    tmp = header.data();
    for (uint8_t i = 0; i < v5_.fileNameEntryFormatCount; i++) {
      // A sequence of file entry format descriptions. Each description
      // consists of a pair of ULEB128 values:
      readULEB(header); // A content type code
      readULEB(header); // A form code using the attribute form codes
    }
    v5_.fileNameEntryFormat.assign(tmp, header.data());
    v5_.fileNamesCount = readULEB(header);
    tmp = header.data();
    for (uint64_t i = 0; i < v5_.fileNamesCount; i++) {
      folly::StringPiece format = v5_.fileNameEntryFormat;
      for (uint8_t f = 0; f < v5_.fileNameEntryFormatCount; f++) {
        readLineNumberAttribute(
            is64Bit_, format, header, debugStr_, debugLineStr_);
      }
    }
    v5_.fileNames.assign(tmp, header.data());
  }
}

bool Dwarf::LineNumberVM::next(folly::StringPiece& program) {
  Dwarf::LineNumberVM::StepResult ret;
  do {
    ret = step(program);
  } while (ret == CONTINUE);

  return (ret == COMMIT);
}

Dwarf::LineNumberVM::FileName Dwarf::LineNumberVM::getFileName(
    uint64_t index) const {
  if (version_ <= 4) {
    FOLLY_SAFE_CHECK(index != 0, "invalid file index 0");
    FileName fn;
    if (index <= v4_.fileNameCount) {
      folly::StringPiece fileNames = v4_.fileNames;
      for (; index; --index) {
        if (!readFileName(fileNames, fn)) {
          abort();
        }
      }
      return fn;
    }

    index -= v4_.fileNameCount;

    folly::StringPiece program = data_;
    for (; index; --index) {
      FOLLY_SAFE_CHECK(nextDefineFile(program, fn), "invalid file index");
    }

    return fn;
  } else {
    FileName fn;
    FOLLY_SAFE_CHECK(index < v5_.fileNamesCount, "invalid file index");
    folly::StringPiece fileNames = v5_.fileNames;
    for (uint64_t i = 0; i < v5_.fileNamesCount; i++) {
      folly::StringPiece format = v5_.fileNameEntryFormat;
      for (uint8_t f = 0; f < v5_.fileNameEntryFormatCount; f++) {
        auto attr = readLineNumberAttribute(
            is64Bit_, format, fileNames, debugStr_, debugLineStr_);
        if (i == index) {
          switch (attr.contentTypeCode) {
            case DW_LNCT_path:
              fn.relativeName = boost::get<folly::StringPiece>(attr.attrValue);
              break;
            case DW_LNCT_directory_index:
              fn.directoryIndex = boost::get<uint64_t>(attr.attrValue);
              break;
          }
        }
      }
    }
    return fn;
  }
}

folly::StringPiece Dwarf::LineNumberVM::getIncludeDirectory(
    uint64_t index) const {
  if (version_ <= 4) {
    if (index == 0) {
      // In DWARF <= 4 the current directory is not represented in the
      // directories field and a directory index of 0 implicitly referred to
      // that directory as found in the DW_AT_comp_dir attribute of the
      // compilation unit debugging information entry.
      return folly::StringPiece();
    }

    FOLLY_SAFE_CHECK(
        index <= v4_.includeDirectoryCount, "invalid include directory");

    folly::StringPiece includeDirectories = v4_.includeDirectories;
    folly::StringPiece dir;
    for (; index; --index) {
      dir = readNullTerminated(includeDirectories);
      if (dir.empty()) {
        abort(); // BUG
      }
    }

    return dir;
  } else {
    FOLLY_SAFE_CHECK(index < v5_.directoriesCount, "invalid file index");
    folly::StringPiece directories = v5_.directories;
    for (uint64_t i = 0; i < v5_.directoriesCount; i++) {
      folly::StringPiece format = v5_.directoryEntryFormat;
      for (uint8_t f = 0; f < v5_.directoryEntryFormatCount; f++) {
        auto attr = readLineNumberAttribute(
            is64Bit_, format, directories, debugStr_, debugLineStr_);
        if (i == index && attr.contentTypeCode == DW_LNCT_path) {
          return boost::get<folly::StringPiece>(attr.attrValue);
        }
      }
    }
    // This could only happen if DWARF5's directory_entry_format doesn't contain
    // a DW_LNCT_path. Highly unlikely, but we shouldn't crash.
    return folly::StringPiece("<directory not found>");
  }
}

bool Dwarf::LineNumberVM::readFileName(
    folly::StringPiece& program, FileName& fn) {
  fn.relativeName = readNullTerminated(program);
  if (fn.relativeName.empty()) {
    return false;
  }
  fn.directoryIndex = readULEB(program);
  // Skip over file size and last modified time
  readULEB(program);
  readULEB(program);
  return true;
}

bool Dwarf::LineNumberVM::nextDefineFile(
    folly::StringPiece& program, FileName& fn) const {
  while (!program.empty()) {
    auto opcode = read<uint8_t>(program);

    if (opcode >= opcodeBase_) { // special opcode
      continue;
    }

    if (opcode != 0) { // standard opcode
      // Skip, slurp the appropriate number of LEB arguments
      uint8_t argCount = standardOpcodeLengths_[opcode - 1];
      while (argCount--) {
        readULEB(program);
      }
      continue;
    }

    // Extended opcode
    auto length = readULEB(program);
    // the opcode itself should be included in the length, so length >= 1
    FOLLY_SAFE_CHECK(length != 0, "invalid extended opcode length");
    read<uint8_t>(program); // extended opcode
    --length;

    if (opcode == DW_LNE_define_file) {
      FOLLY_SAFE_CHECK(version_ < 5, "DW_LNE_define_file deprecated in DWARF5");
      FOLLY_SAFE_CHECK(
          readFileName(program, fn),
          "invalid empty file in DW_LNE_define_file");
      return true;
    }

    program.advance(length);
    continue;
  }

  return false;
}

Dwarf::LineNumberVM::StepResult Dwarf::LineNumberVM::step(
    folly::StringPiece& program) {
  auto opcode = read<uint8_t>(program);

  if (opcode >= opcodeBase_) { // special opcode
    uint8_t adjustedOpcode = opcode - opcodeBase_;
    uint8_t opAdvance = adjustedOpcode / lineRange_;

    address_ += minLength_ * opAdvance;
    line_ += lineBase_ + adjustedOpcode % lineRange_;

    basicBlock_ = false;
    prologueEnd_ = false;
    epilogueBegin_ = false;
    discriminator_ = 0;
    return COMMIT;
  }

  if (opcode != 0) { // standard opcode
    // Only interpret opcodes that are recognized by the version we're parsing;
    // the others are vendor extensions and we should ignore them.
    switch (opcode) {
      case DW_LNS_copy:
        basicBlock_ = false;
        prologueEnd_ = false;
        epilogueBegin_ = false;
        discriminator_ = 0;
        return COMMIT;
      case DW_LNS_advance_pc:
        address_ += minLength_ * readULEB(program);
        return CONTINUE;
      case DW_LNS_advance_line:
        line_ += readSLEB(program);
        return CONTINUE;
      case DW_LNS_set_file:
        file_ = readULEB(program);
        return CONTINUE;
      case DW_LNS_set_column:
        column_ = readULEB(program);
        return CONTINUE;
      case DW_LNS_negate_stmt:
        isStmt_ = !isStmt_;
        return CONTINUE;
      case DW_LNS_set_basic_block:
        basicBlock_ = true;
        return CONTINUE;
      case DW_LNS_const_add_pc:
        address_ += minLength_ * ((255 - opcodeBase_) / lineRange_);
        return CONTINUE;
      case DW_LNS_fixed_advance_pc:
        address_ += read<uint16_t>(program);
        return CONTINUE;
      case DW_LNS_set_prologue_end:
        if (version_ == 2) {
          break; // not supported in version 2
        }
        prologueEnd_ = true;
        return CONTINUE;
      case DW_LNS_set_epilogue_begin:
        if (version_ == 2) {
          break; // not supported in version 2
        }
        epilogueBegin_ = true;
        return CONTINUE;
      case DW_LNS_set_isa:
        if (version_ == 2) {
          break; // not supported in version 2
        }
        isa_ = readULEB(program);
        return CONTINUE;
    }

    // Unrecognized standard opcode, slurp the appropriate number of LEB
    // arguments.
    uint8_t argCount = standardOpcodeLengths_[opcode - 1];
    while (argCount--) {
      readULEB(program);
    }
    return CONTINUE;
  }

  // Extended opcode
  auto length = readULEB(program);
  // the opcode itself should be included in the length, so length >= 1
  FOLLY_SAFE_CHECK(length != 0, "invalid extended opcode length");
  auto extendedOpcode = read<uint8_t>(program);
  --length;

  switch (extendedOpcode) {
    case DW_LNE_end_sequence:
      return END;
    case DW_LNE_set_address:
      address_ = read<uintptr_t>(program);
      return CONTINUE;
    case DW_LNE_define_file:
      FOLLY_SAFE_CHECK(version_ < 5, "DW_LNE_define_file deprecated in DWARF5");
      // We can't process DW_LNE_define_file here, as it would require us to
      // use unbounded amounts of state (ie. use the heap).  We'll do a second
      // pass (using nextDefineFile()) if necessary.
      break;
#if !defined(__FreeBSD__)
    case DW_LNE_set_discriminator:
      discriminator_ = readULEB(program);
      return CONTINUE;
#endif
  }

  // Unrecognized extended opcode
  program.advance(length);
  return CONTINUE;
}

Path Dwarf::LineNumberVM::getFullFileName(uint64_t index) const {
  auto fn = getFileName(index);
  return Path(
      // DWARF <= 4: the current dir is not represented in the CU's Line Number
      // Program Header and relies on the CU's DW_AT_comp_dir.
      // DWARF 5: the current directory is explicitly present.
      version_ == 5 ? "" : compilationDirectory_,
      getIncludeDirectory(fn.directoryIndex),
      fn.relativeName);
}

bool Dwarf::LineNumberVM::findAddress(
    uintptr_t target, Path& file, uint64_t& line) {
  folly::StringPiece program = data_;

  // Within each sequence of instructions, the address may only increase.
  // Unfortunately, within the same compilation unit, sequences may appear
  // in any order.  So any sequence is a candidate if it starts at an address
  // <= the target address, and we know we've found the target address if
  // a candidate crosses the target address.
  enum State {
    START,
    LOW_SEQ, // candidate
    HIGH_SEQ
  };
  State state = START;
  reset();

  uint64_t prevFile = 0;
  uint64_t prevLine = 0;
  while (!program.empty()) {
    bool seqEnd = !next(program);

    if (state == START) {
      if (!seqEnd) {
        state = address_ <= target ? LOW_SEQ : HIGH_SEQ;
      }
    }

    if (state == LOW_SEQ) {
      if (address_ > target) {
        // Found it!  Note that ">" is indeed correct (not ">="), as each
        // sequence is guaranteed to have one entry past-the-end (emitted by
        // DW_LNE_end_sequence)

        // NOTE: In DWARF <= 4 the file register is non-zero.
        //   See DWARF 4: 6.2.4 The Line Number Program Header
        //   "The line number program assigns numbers to each of the file
        //   entries in order, beginning with 1, and uses those numbers instead
        //   of file names in the file register."
        // DWARF 5 has a different include directory/file header and 0 is valid.
        if (version_ <= 4 && prevFile == 0) {
          return false;
        }
        file = getFullFileName(prevFile);
        line = prevLine;
        return true;
      }
      prevFile = file_;
      prevLine = line_;
    }

    if (seqEnd) {
      state = START;
      reset();
    }
  }

  return false;
}

} // namespace symbolizer
} // namespace folly

#endif // FOLLY_HAVE_DWARF
