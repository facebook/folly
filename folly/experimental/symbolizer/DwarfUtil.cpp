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

#include <folly/experimental/symbolizer/DwarfUtil.h>

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

folly::StringPiece getStringFromStringSection(
    folly::StringPiece str, uint64_t offset) {
  FOLLY_SAFE_CHECK(offset < str.size(), "invalid string offset");
  str.advance(offset);
  return readNullTerminated(str);
}

AttributeSpec readAttributeSpec(folly::StringPiece& sp) {
  AttributeSpec spec;
  spec.name = readULEB(sp);
  spec.form = readULEB(sp);
  if (spec.form == DW_FORM_implicit_const) {
    spec.implicitConst = readSLEB(sp);
  }
  return spec;
}

// Reads an abbreviation from a StringPiece, return true if at end; advance sp
bool readAbbreviation(folly::StringPiece& section, DIEAbbreviation& abbr) {
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

namespace {

DIEAbbreviation getAbbreviation(
    folly::StringPiece debugAbbrev, uint64_t code, uint64_t offset) {
  // Linear search in the .debug_abbrev section, starting at offset
  debugAbbrev.advance(offset);

  DIEAbbreviation abbr;
  while (readAbbreviation(debugAbbrev, abbr)) {
    if (abbr.code == code) {
      return abbr;
    }
  }

  FOLLY_SAFE_CHECK(false, "could not find abbreviation code");
}

} // namespace

CompilationUnit getCompilationUnit(
    const DebugSections& debugSections, uint64_t offset) {
  const folly::StringPiece debugInfo = debugSections.debugInfo;
  FOLLY_SAFE_DCHECK(offset < debugInfo.size(), "unexpected offset");
  CompilationUnit cu;
  cu.debugSections = debugSections;

  folly::StringPiece chunk(debugInfo);
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
  cu.firstDie = chunk.data() - debugInfo.data();
  if (cu.version < 5) {
    return cu;
  }

  Die die = getDieAtOffset(cu, cu.firstDie);
  if (die.abbr.tag != DW_TAG_compile_unit &&
      die.abbr.tag != DW_TAG_skeleton_unit) {
    return cu;
  }

  // Read the DW_AT_*_base attributes.
  // Attributes which use FORMs relative to these base attrs
  // will not have valid values during this first pass!
  forEachAttribute(cu, die, [&](const Attribute& attr) {
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

Die getDieAtOffset(const CompilationUnit& cu, uint64_t offset) {
  const folly::StringPiece debugInfo = cu.debugSections.debugInfo;
  FOLLY_SAFE_DCHECK(offset < debugInfo.size(), "unexpected offset");
  Die die;
  folly::StringPiece sp = folly::StringPiece{
      debugInfo.data() + offset, debugInfo.data() + cu.offset + cu.size};
  die.offset = offset;
  die.is64Bit = cu.is64Bit;
  auto code = readULEB(sp);
  die.code = code;
  if (code == 0) {
    return die;
  }
  die.attrOffset = sp.data() - debugInfo.data() - offset;
  die.abbr = !cu.abbrCache.empty() && die.code < kMaxAbbreviationEntries
      ? cu.abbrCache[die.code - 1]
      : getAbbreviation(
            cu.debugSections.debugAbbrev, die.code, cu.abbrevOffset);

  return die;
}

Attribute readAttribute(
    const CompilationUnit& cu,
    const Die& die,
    AttributeSpec spec,
    folly::StringPiece& info) {
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
    auto sp = cu.debugSections.debugStrOffsets.subpiece(
        *cu.strOffsetsBase +
        index * (cu.is64Bit ? sizeof(uint64_t) : sizeof(uint32_t)));
    uint64_t strOffset = readOffset(sp, cu.is64Bit);
    return getStringFromStringSection(cu.debugSections.debugStr, strOffset);
  };

  auto readDebugAddr = [&](uint64_t index) {
    if (!cu.addrBase.has_value()) {
      return uint64_t(0);
    }
    // DWARF 5: 7.27 Address Table
    // The DW_AT_addr_base attribute points to the first entry following the
    // header. The entries are indexed sequentially from this base entry,
    // starting from 0.
    auto sp = cu.debugSections.debugAddr.subpiece(
        *cu.addrBase + index * sizeof(uint64_t));
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
          getStringFromStringSection(
              cu.debugSections.debugStr, readOffset(info, die.is64Bit))};
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
              cu.debugSections.debugLineStr, readOffset(info, die.is64Bit))};

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
      auto sp = cu.debugSections.debugRnglists.subpiece(
          *cu.rnglistsBase + index * offsetSize);
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
      auto sp = cu.debugSections.debugLoclists.subpiece(
          *cu.loclistsBase + index * offsetSize);
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
size_t forEachAttribute(
    const CompilationUnit& cu,
    const Die& die,
    folly::FunctionRef<bool(const Attribute&)> f) {
  auto attrs = die.abbr.attributes;
  auto values = folly::StringPiece{
      cu.debugSections.debugInfo.data() + die.offset + die.attrOffset,
      cu.debugSections.debugInfo.data() + cu.offset + cu.size};
  while (auto spec = readAttributeSpec(attrs)) {
    auto attr = readAttribute(cu, die, spec, values);
    if (!f(attr)) {
      return static_cast<size_t>(-1);
    }
  }
  return values.data() - cu.debugSections.debugInfo.data();
}

} // namespace symbolizer
} // namespace folly

#endif // FOLLY_HAVE_DWARF
