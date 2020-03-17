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

#include <dwarf.h>
#include <type_traits>

namespace folly {
namespace symbolizer {

namespace detail {

// Abbreviation for a Debugging Information Entry.
struct DIEAbbreviation {
  uint64_t code;
  uint64_t tag;
  bool hasChildren = false;

  folly::StringPiece attributes;
};

struct CompilationUnit {
  bool is64Bit;
  uint8_t version;
  uint8_t addrSize;
  // Offset in .debug_info of this compilation unit.
  uint32_t offset;
  uint32_t size;
  // Offset in .debug_info for the first DIE in this compilation unit.
  uint32_t firstDie;
  uint64_t abbrevOffset;
  // Only the CompilationUnit that contains the caller functions needs this
  // cache.
  // Indexed by (abbr.code - 1) if (abbr.code - 1) < abbrCache.size();
  folly::Range<DIEAbbreviation*> abbrCache;
};

struct Die {
  bool is64Bit;
  // Offset from start to first attribute
  uint8_t attrOffset;
  // Offset within debug info.
  uint32_t offset;
  uint64_t code;
  DIEAbbreviation abbr;
};

struct AttributeSpec {
  uint64_t name = 0;
  uint64_t form = 0;

  explicit operator bool() const {
    return name != 0 || form != 0;
  }
};

struct Attribute {
  AttributeSpec spec;
  const Die& die;
  boost::variant<uint64_t, folly::StringPiece> attrValue;
};

struct CodeLocation {
  Die die;
  folly::Optional<uint64_t> file;
  folly::Optional<uint64_t> line;
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
  return {readULEB(sp), readULEB(sp)};
}

// Reads an abbreviation from a StringPiece, return true if at end; advance sp
bool readAbbreviation(
    folly::StringPiece& section,
    detail::DIEAbbreviation& abbr) {
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
    folly::StringPiece str,
    uint64_t offset) {
  FOLLY_SAFE_CHECK(offset < str.size(), "invalid string offset");
  str.advance(offset);
  return readNullTerminated(str);
}

detail::Attribute readAttribute(
    const detail::Die& die,
    detail::AttributeSpec spec,
    folly::StringPiece& info,
    folly::StringPiece str) {
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
      return {spec,
              die,
              getStringFromStringSection(str, readOffset(info, die.is64Bit))};
    case DW_FORM_indirect: // form is explicitly specified
      // Update spec with the actual FORM.
      spec.form = readULEB(info);
      return readAttribute(die, spec, info, str);
    default:
      FOLLY_SAFE_CHECK(false, "invalid attribute form");
  }
  return {spec, die, 0};
}

detail::CompilationUnit getCompilationUnit(
    folly::StringPiece info,
    uint64_t offset) {
  FOLLY_SAFE_DCHECK(offset < info.size(), "unexpected offset");
  detail::CompilationUnit cu;
  folly::StringPiece chunk(info);
  cu.offset = offset;
  chunk.advance(offset);

  auto initialLength = read<uint32_t>(chunk);
  cu.is64Bit = (initialLength == (uint32_t)-1);
  cu.size = cu.is64Bit ? read<uint64_t>(chunk) : initialLength;
  FOLLY_SAFE_CHECK(cu.size <= chunk.size(), "invalid chunk size");
  cu.size += cu.is64Bit ? 12 : 4;

  cu.version = read<uint16_t>(chunk);
  FOLLY_SAFE_CHECK(cu.version >= 2 && cu.version <= 4, "invalid info version");
  cu.abbrevOffset = readOffset(chunk, cu.is64Bit);
  cu.addrSize = read<uint8_t>(chunk);
  FOLLY_SAFE_CHECK(cu.addrSize == sizeof(uintptr_t), "invalid address size");

  cu.firstDie = chunk.data() - info.data();
  return cu;
}

// Finds the Compilation Unit starting at offset.
detail::CompilationUnit findCompilationUnit(
    folly::StringPiece info,
    uint64_t targetOffset) {
  FOLLY_SAFE_DCHECK(targetOffset < info.size(), "unexpected target address");
  uint64_t offset = 0;
  while (offset < info.size()) {
    folly::StringPiece chunk(info);
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
  return getCompilationUnit(info, offset);
}

void readCompilationUnitAbbrs(
    folly::StringPiece abbrev,
    detail::CompilationUnit& cu) {
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
      debugInfo_(getSection(".debug_info")),
      debugAbbrev_(getSection(".debug_abbrev")),
      debugLine_(getSection(".debug_line")),
      debugStr_(getSection(".debug_str")),
      debugAranges_(getSection(".debug_aranges")),
      debugRanges_(getSection(".debug_ranges")) {
  // Optional sections:
  //  - debugAranges_: for fast address range lookup.
  //     If missing .debug_info can be used - but it's much slower (linear
  //     scan).
  //  - debugRanges_: contains non-contiguous address ranges of debugging
  //    information entries. Used for inline function address lookup.
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
  is64Bit_ = (initialLength == (uint32_t)-1);
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

detail::DIEAbbreviation Dwarf::getAbbreviation(uint64_t code, uint64_t offset)
    const {
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
    uintptr_t address,
    StringPiece aranges,
    uint64_t& offset) {
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
 * Find the @locationInfo for @address in the compilation unit represented
 * by the @sp .debug_info entry.
 * Returns whether the address was found.
 * Advances @sp to the next entry in .debug_info.
 */
bool Dwarf::findLocation(
    uintptr_t address,
    const LocationInfoMode mode,
    detail::CompilationUnit& cu,
    LocationInfo& locationInfo,
    folly::Range<SymbolizedFrame*> inlineFrames) const {
  detail::Die die = getDieAtOffset(cu, cu.firstDie);
  // Partial compilation unit (DW_TAG_partial_unit) is not supported.
  FOLLY_SAFE_CHECK(
      die.abbr.tag == DW_TAG_compile_unit, "expecting compile unit entry");

  // Offset in .debug_line for the line number VM program for this
  // compilation unit
  bool foundLineOffset = false;
  uint64_t lineOffset = 0;
  folly::StringPiece compilationDirectory;
  folly::StringPiece mainFileName;

  forEachAttribute(cu, die, [&](const detail::Attribute& attr) {
    switch (attr.spec.name) {
      case DW_AT_stmt_list:
        // Offset in .debug_line for the line number VM program for this
        // compilation unit
        lineOffset = boost::get<uint64_t>(attr.attrValue);
        foundLineOffset = true;
        break;
      case DW_AT_comp_dir:
        // Compilation directory
        compilationDirectory = boost::get<folly::StringPiece>(attr.attrValue);
        break;
      case DW_AT_name:
        // File name of main file being compiled
        mainFileName = boost::get<folly::StringPiece>(attr.attrValue);
        break;
    }
    // Iterate through all attributes until find all above.
    return true;
  });

  if (!mainFileName.empty()) {
    locationInfo.hasMainFile = true;
    locationInfo.mainFile = Path(compilationDirectory, "", mainFileName);
  }

  if (!foundLineOffset) {
    return false;
  }

  folly::StringPiece lineSection(debugLine_);
  lineSection.advance(lineOffset);
  LineNumberVM lineVM(lineSection, compilationDirectory);

  // Execute line number VM program to find file and line
  locationInfo.hasFileAndLine =
      lineVM.findAddress(address, locationInfo.file, locationInfo.line);

  // Look up whether inline function.
  if (mode == LocationInfoMode::FULL_WITH_INLINE && !inlineFrames.empty() &&
      locationInfo.hasFileAndLine) {
    // Re-get the compilation unit with abbreviation cached.
    std::array<detail::DIEAbbreviation, kMaxAbbreviationEntries> abbrs;
    cu.abbrCache = folly::range(abbrs);
    readCompilationUnitAbbrs(debugAbbrev_, cu);
    detail::Die subprogram;
    findSubProgramDieForAddress(cu, die, address, subprogram);
    // Subprogram is the DIE of caller function.
    if (subprogram.abbr.hasChildren) {
      size_t size = std::min<size_t>(
          Dwarf::kMaxInlineLocationInfoPerFrame, inlineFrames.size());
      detail::CodeLocation inlineLocs[size];
      auto inlineLocsRange =
          folly::Range<detail::CodeLocation*>(inlineLocs, size);
      findInlinedSubroutineDieForAddress(
          cu, subprogram, address, inlineLocsRange);
      size_t numFound = inlineLocsRange.data() - inlineLocs;

      // The line in locationInfo is the line in the deepest inline functions,
      // and the lines in inlineLocationInfo are the line of callers in call
      // sequence. A shuffle is needed to match lines with their function name.
      uint64_t callerLine = locationInfo.line;
      inlineLocsRange =
          folly::Range<detail::CodeLocation*>(inlineLocs, numFound);
      // Put inline frames from inside to outside (callee is before caller).
      std::reverse(inlineLocsRange.begin(), inlineLocsRange.end());
      auto inlineFrameIter = inlineFrames.begin();
      for (auto inlineLocIter = inlineLocsRange.begin();
           inlineLocIter != inlineLocsRange.end();
           inlineLocIter++, inlineFrameIter++) {
        inlineFrameIter->location.line = callerLine;
        if (!inlineLocIter->line) {
          break;
        }
        callerLine = inlineLocIter->line.value();

        folly::Optional<folly::StringPiece> linkageName;
        folly::Optional<folly::StringPiece> name;
        forEachAttribute(
            cu, inlineLocIter->die, [&](const detail::Attribute& attr) {
              switch (attr.spec.name) {
                case DW_AT_linkage_name:
                  linkageName = boost::get<folly::StringPiece>(attr.attrValue);
                  break;
                case DW_AT_name:
                  name = boost::get<folly::StringPiece>(attr.attrValue);
                  break;
                case DW_AT_decl_file:
                  // If the inline function is declared and defined in the same
                  // file, the declaration subprogram may not have attribute
                  // DW_AT_decl_file, then we need to find decl_file in the
                  // definition subprogram.
                  if (!inlineLocIter->file) {
                    inlineLocIter->file = boost::get<uint64_t>(attr.attrValue);
                  }
                  break;
              }
              return !inlineLocIter->file || !name || !linkageName;
            });
        if (!(name || linkageName) || !inlineLocIter->file) {
          break;
        }

        inlineFrameIter->found = true;
        inlineFrameIter->addr = address;
        inlineFrameIter->name =
            linkageName ? linkageName.value().data() : name.value().data();
        inlineFrameIter->location.hasFileAndLine = true;
        inlineFrameIter->location.name =
            linkageName ? linkageName.value() : name.value();
        inlineFrameIter->location.file =
            lineVM.getFullFileName(inlineLocIter->file.value());
      }
      locationInfo.line = callerLine;
    }
  }

  return locationInfo.hasFileAndLine;
}

bool Dwarf::findAddress(
    uintptr_t address,
    LocationInfoMode mode,
    LocationInfo& locationInfo,
    folly::Range<SymbolizedFrame*> inlineFrames) const {
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
      auto unit = getCompilationUnit(debugInfo_, offset);
      findLocation(address, mode, unit, locationInfo, inlineFrames);
      return locationInfo.hasFileAndLine;
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
  while (offset < debugInfo_.size() && !locationInfo.hasFileAndLine) {
    auto unit = getCompilationUnit(debugInfo_, offset);
    offset += unit.size;
    findLocation(address, mode, unit, locationInfo, inlineFrames);
  }
  return locationInfo.hasFileAndLine;
}

detail::Die Dwarf::getDieAtOffset(
    const detail::CompilationUnit& cu,
    uint64_t offset) const {
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
    const detail::CompilationUnit& cu,
    const detail::Die& die) const {
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

/*
 * Iterate over all attributes of the given DIE, calling the given callable
 * for each. Iteration is stopped early if any of the calls return false.
 */
size_t Dwarf::forEachAttribute(
    const detail::CompilationUnit& cu,
    const detail::Die& die,
    folly::FunctionRef<bool(const detail::Attribute& die)> f) const {
  auto attrs = die.abbr.attributes;
  auto values =
      folly::StringPiece{debugInfo_.data() + die.offset + die.attrOffset,
                         debugInfo_.data() + cu.offset + cu.size};
  while (auto spec = readAttributeSpec(attrs)) {
    auto attr = readAttribute(die, spec, values, debugStr_);
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

bool Dwarf::isAddrInRangeList(uint64_t address, size_t offset, uint8_t addrSize)
    const {
  FOLLY_SAFE_CHECK(addrSize == 4 || addrSize == 8, "wrong address size");
  if (debugRanges_.empty()) {
    return false;
  }

  const bool is64BitAddr = addrSize == 8;
  folly::StringPiece sp = debugRanges_;
  sp.advance(offset);
  const uint64_t maxAddr = is64BitAddr ? std::numeric_limits<uint64_t>::max()
                                       : std::numeric_limits<uint32_t>::max();
  uint64_t baseAddr = 0;
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
    if (address >= begin + baseAddr && address < end + baseAddr) {
      return true;
    }
  };

  return false;
}

void Dwarf::findSubProgramDieForAddress(
    const detail::CompilationUnit& cu,
    const detail::Die& die,
    uint64_t address,
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
            // Value of DW_AT_high_pc attribute can be an address
            // (DW_FORM_addr) or an offset (DW_FORM_data).
            isHighPcAddr = (attr.spec.form == DW_FORM_addr);
            highPc = boost::get<uint64_t>(attr.attrValue);
            break;
        }
        // Iterate through all attributes until find all above.
        return true;
      });
      if ((lowPc && highPc && isHighPcAddr && address >= *lowPc &&
           address < (*isHighPcAddr ? *highPc : *lowPc + *highPc)) ||
          (rangeOffset &&
           isAddrInRangeList(address, *rangeOffset, cu.addrSize))) {
        subprogram = childDie;
        return false;
      }
    } else if (
        childDie.abbr.tag == DW_TAG_namespace ||
        childDie.abbr.tag == DW_TAG_class_type) {
      findSubProgramDieForAddress(cu, childDie, address, subprogram);
    }
    // Iterates through children until find the inline subprogram.
    return true;
  });
}

void Dwarf::findInlinedSubroutineDieForAddress(
    const detail::CompilationUnit& cu,
    const detail::Die& die,
    uint64_t address,
    folly::Range<detail::CodeLocation*>& isrLoc) const {
  if (isrLoc.empty()) {
    return;
  }
  forEachChild(cu, die, [&](const detail::Die& childDie) {
    if (childDie.abbr.tag != DW_TAG_inlined_subroutine) {
      return true; // Skip DIE that is not inlined_subroutine.
    }
    folly::Optional<uint64_t> lowPc;
    folly::Optional<uint64_t> highPc;
    folly::Optional<bool> isHighPcAddr;
    folly::Optional<uint64_t> origin;
    folly::Optional<uint64_t> originRefType;
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
          // Value of DW_AT_high_pc attribute can be an address
          // (DW_FORM_addr) or an offset (DW_FORM_data).
          isHighPcAddr = (attr.spec.form == DW_FORM_addr);
          highPc = boost::get<uint64_t>(attr.attrValue);
          break;
        case DW_AT_abstract_origin:
          originRefType = attr.spec.form;
          origin = boost::get<uint64_t>(attr.attrValue);
          break;
        case DW_AT_call_line:
          callLine = boost::get<uint64_t>(attr.attrValue);
          break;
      }
      // Iterate through all until find all above attributes.
      return true;
    });
    if (!origin || !originRefType || !callLine) {
      // Missing required fields. Keep searching other children.
      return true;
    }
    if ((!lowPc || !highPc || !isHighPcAddr || address < *lowPc ||
         address >= (*isHighPcAddr ? *highPc : *lowPc + *highPc)) &&
        (!rangeOffset ||
         !isAddrInRangeList(address, rangeOffset.value(), cu.addrSize))) {
      // Address doesn't match. Keep searching other children.
      return true;
    }

    isrLoc[0].line = *callLine;
    auto setLocationInfo = [&](const detail::CompilationUnit& srcu,
                               uint64_t dieOffset) {
      auto declDie = getDieAtOffset(srcu, dieOffset);
      // If the inline function is declared and defined in different file, then
      // DW_AT_decl_file attribute of the declaration may contain the definition
      // file name.
      isrLoc[0].file = getAttribute<uint64_t>(srcu, declDie, DW_AT_decl_file);
      // Jump to the actual function definition instead of declaration for name
      // and line info.
      isrLoc[0].die = findDefinitionDie(srcu, declDie);
      isrLoc.advance(1);
    };
    if (*originRefType == DW_FORM_ref1 || *originRefType == DW_FORM_ref2 ||
        *originRefType == DW_FORM_ref4 || *originRefType == DW_FORM_ref8 ||
        *originRefType == DW_FORM_ref_udata) {
      // These reference types identify any debugging information entry within
      // the containing unit. This type of reference is an offset from the first
      // byte of the compilation header for the compilation unit containing the
      // reference.
      setLocationInfo(cu, cu.offset + *origin);
      findInlinedSubroutineDieForAddress(cu, childDie, address, isrLoc);
    } else if (*originRefType == DW_FORM_ref_addr) {
      // DW_FORM_ref_addr identifies any debugging information entry
      // within a .debug_info section; in particular, it may refer to an entry
      // in a different compilation unit from the unit containing the reference,
      // and may refer to an entry in a different shared object.
      auto srcu = findCompilationUnit(debugInfo_, *origin);
      setLocationInfo(srcu, *origin);
      findInlinedSubroutineDieForAddress(srcu, childDie, address, isrLoc);
    }

    return false;
  });
}

Dwarf::LineNumberVM::LineNumberVM(
    folly::StringPiece data,
    folly::StringPiece compilationDirectory)
    : compilationDirectory_(compilationDirectory) {
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

void Dwarf::LineNumberVM::init() {
  version_ = read<uint16_t>(data_);
  FOLLY_SAFE_CHECK(
      version_ >= 2 && version_ <= 4, "invalid version in line number VM");
  uint64_t headerLength = readOffset(data_, is64Bit_);
  FOLLY_SAFE_CHECK(
      headerLength <= data_.size(), "invalid line number VM header length");
  folly::StringPiece header(data_.data(), headerLength);
  data_.assign(header.end(), data_.end());

  minLength_ = read<uint8_t>(header);
  if (version_ == 4) { // Version 2 and 3 records don't have this
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

  // We don't want to use heap, so we don't keep an unbounded amount of state.
  // We'll just skip over include directories and file names here, and
  // we'll loop again when we actually need to retrieve one.
  folly::StringPiece sp;
  const char* tmp = header.data();
  includeDirectoryCount_ = 0;
  while (!(sp = readNullTerminated(header)).empty()) {
    ++includeDirectoryCount_;
  }
  includeDirectories_.assign(tmp, header.data());

  tmp = header.data();
  FileName fn;
  fileNameCount_ = 0;
  while (readFileName(header, fn)) {
    ++fileNameCount_;
  }
  fileNames_.assign(tmp, header.data());
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
  FOLLY_SAFE_CHECK(index != 0, "invalid file index 0");

  FileName fn;
  if (index <= fileNameCount_) {
    folly::StringPiece fileNames = fileNames_;
    for (; index; --index) {
      if (!readFileName(fileNames, fn)) {
        abort();
      }
    }
    return fn;
  }

  index -= fileNameCount_;

  folly::StringPiece program = data_;
  for (; index; --index) {
    FOLLY_SAFE_CHECK(nextDefineFile(program, fn), "invalid file index");
  }

  return fn;
}

folly::StringPiece Dwarf::LineNumberVM::getIncludeDirectory(
    uint64_t index) const {
  if (index == 0) {
    return folly::StringPiece();
  }

  FOLLY_SAFE_CHECK(
      index <= includeDirectoryCount_, "invalid include directory");

  folly::StringPiece includeDirectories = includeDirectories_;
  folly::StringPiece dir;
  for (; index; --index) {
    dir = readNullTerminated(includeDirectories);
    if (dir.empty()) {
      abort(); // BUG
    }
  }

  return dir;
}

bool Dwarf::LineNumberVM::readFileName(
    folly::StringPiece& program,
    FileName& fn) {
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
    folly::StringPiece& program,
    FileName& fn) const {
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
      // We can't process DW_LNE_define_file here, as it would require us to
      // use unbounded amounts of state (ie. use the heap).  We'll do a second
      // pass (using nextDefineFile()) if necessary.
      break;
    case DW_LNE_set_discriminator:
      discriminator_ = readULEB(program);
      return CONTINUE;
  }

  // Unrecognized extended opcode
  program.advance(length);
  return CONTINUE;
}

bool Dwarf::LineNumberVM::findAddress(
    uintptr_t target,
    Path& file,
    uint64_t& line) {
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
        if (prevFile == 0) {
          return false;
        }
        auto fn = getFileName(prevFile);
        file = Path(
            compilationDirectory_,
            getIncludeDirectory(fn.directoryIndex),
            fn.relativeName);
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
