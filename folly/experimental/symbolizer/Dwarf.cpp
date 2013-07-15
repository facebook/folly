/*
 * Copyright 2013 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "folly/experimental/symbolizer/Dwarf.h"

#include <type_traits>

#include <dwarf.h>

namespace folly {
namespace symbolizer {

Dwarf::Dwarf(const ElfFile* elf) : elf_(elf) {
  init();
}

Dwarf::Section::Section(folly::StringPiece d) : is64Bit_(false), data_(d) {
}

namespace {

// All following read* functions read from a StringPiece, advancing the
// StringPiece, and throwing an exception if there's not enough room

// Read (bitwise) one object of type T
template <class T>
typename std::enable_if<std::is_pod<T>::value, T>::type
read(folly::StringPiece& sp) {
  enforce(sp.size() >= sizeof(T), "underflow");
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
    r |= -(1ULL << shift);  // sign extend
  }

  return r;
}

// Read a value of "section offset" type, which may be 4 or 8 bytes
uint64_t readOffset(folly::StringPiece& sp, bool is64Bit) {
  return is64Bit ? read<uint64_t>(sp) : read<uint32_t>(sp);
}

// Read "len" bytes
folly::StringPiece readBytes(folly::StringPiece& sp, uint64_t len) {
  enforce(len >= sp.size(), "invalid string length");
  folly::StringPiece ret(sp.data(), len);
  sp.advance(len);
  return ret;
}

// Read a null-terminated string
folly::StringPiece readNullTerminated(folly::StringPiece& sp) {
  const char* p = static_cast<const char*>(
      memchr(sp.data(), 0, sp.size()));
  enforce(p, "invalid null-terminated string");
  folly::StringPiece ret(sp.data(), p);
  sp.assign(p + 1, sp.end());
  return ret;
}

// Skip over padding until sp.data() - start is a multiple of alignment
void skipPadding(folly::StringPiece& sp, const char* start, size_t alignment) {
  size_t remainder = (sp.data() - start) % alignment;
  if (remainder) {
    enforce(alignment - remainder <= sp.size(), "invalid padding");
    sp.advance(alignment - remainder);
  }
}

void stripSlashes(folly::StringPiece& sp, bool keepInitialSlash) {
  if (sp.empty()) {
    return;
  }

  const char* p = sp.begin();
  for (; p != sp.end() && *p == '/'; ++p);

  const char* q = sp.end();
  for (; q != p && q[-1] == '/'; --q);

  if (keepInitialSlash && p != sp.begin()) {
    --p;
  }

  sp.assign(p, q);
}

}  // namespace

Dwarf::Path::Path(folly::StringPiece baseDir, folly::StringPiece subDir,
                  folly::StringPiece file)
  : baseDir_(baseDir),
    subDir_(subDir),
    file_(file) {
  using std::swap;

  // Normalize
  if (file_.empty()) {
    baseDir_.clear();
    subDir_.clear();
    return;
  }

  if (file_[0] == '/') {
    // file_ is absolute
    baseDir_.clear();
    subDir_.clear();
  }

  if (!subDir_.empty() && subDir_[0] == '/') {
    baseDir_.clear();  // subDir_ is absolute
  }

  // Make sure that baseDir_ isn't empty; subDir_ may be
  if (baseDir_.empty()) {
    swap(baseDir_, subDir_);
  }

  stripSlashes(baseDir_, true);  // keep leading slash if it exists
  stripSlashes(subDir_, false);
  stripSlashes(file_, false);
}

size_t Dwarf::Path::size() const {
  return
    baseDir_.size() + !subDir_.empty() + subDir_.size() + !file_.empty() +
    file_.size();
}

size_t Dwarf::Path::toBuffer(char* buf, size_t bufSize) const {
  size_t totalSize = 0;

  auto append = [&] (folly::StringPiece sp) {
    if (bufSize >= 2) {
      size_t toCopy = std::min(sp.size(), bufSize - 1);
      memcpy(buf, sp.data(), toCopy);
      buf += toCopy;
      bufSize -= toCopy;
    }
    totalSize += sp.size();
  };

  if (!baseDir_.empty()) {
    append(baseDir_);
  }
  if (!subDir_.empty()) {
    assert(!baseDir_.empty());
    append("/");
    append(subDir_);
  }
  if (!file_.empty()) {
    append("/");
    append(file_);
  }
  if (bufSize) {
    *buf = '\0';
  }
  assert(totalSize == size());
  return totalSize;
}

void Dwarf::Path::toString(std::string& dest) const {
  size_t initialSize = dest.size();
  dest.reserve(initialSize + size());
  if (!baseDir_.empty()) {
    dest.append(baseDir_.begin(), baseDir_.end());
  }
  if (!subDir_.empty()) {
    assert(!baseDir_.empty());
    dest.push_back('/');
    dest.append(subDir_.begin(), subDir_.end());
  }
  if (!file_.empty()) {
    dest.push_back('/');
    dest.append(file_.begin(), file_.end());
  }
  assert(dest.size() == initialSize + size());
}

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
  enforce(length <= chunk.size(), "invalid DWARF section");
  chunk.reset(chunk.data(), length);
  data_.assign(chunk.end(), data_.end());
  return true;
}

bool Dwarf::getSection(const char* name, folly::StringPiece* section) const {
  const ElfW(Shdr)* elfSection = elf_->getSectionByName(name);
  if (!elfSection) {
    return false;
  }

  *section = elf_->getSectionBody(*elfSection);
  return true;
}

void Dwarf::init() {
  // Make sure that all .debug_* sections exist
  if (!getSection(".debug_info", &info_) ||
      !getSection(".debug_abbrev", &abbrev_) ||
      !getSection(".debug_aranges", &aranges_) ||
      !getSection(".debug_line", &line_) ||
      !getSection(".debug_str", &strings_)) {
    elf_ = nullptr;
    return;
  }
  getSection(".debug_str", &strings_);
}

bool Dwarf::readAbbreviation(folly::StringPiece& section,
                             DIEAbbreviation& abbr) {
  // abbreviation code
  abbr.code = readULEB(section);
  if (abbr.code == 0) {
    return false;
  }

  // abbreviation tag
  abbr.tag = readULEB(section);

  // does this entry have children?
  abbr.hasChildren = (read<uint8_t>(section) != DW_CHILDREN_no);

  // attributes
  const char* attributeBegin = section.data();
  for (;;) {
    enforce(!section.empty(), "invalid attribute section");
    auto attr = readAttribute(section);
    if (attr.name == 0 && attr.form == 0) {
      break;
    }
  }

  abbr.attributes.assign(attributeBegin, section.data());
  return true;
}

Dwarf::DIEAbbreviation::Attribute Dwarf::readAttribute(
    folly::StringPiece& sp) {
  return { readULEB(sp), readULEB(sp) };
}

Dwarf::DIEAbbreviation Dwarf::getAbbreviation(uint64_t code, uint64_t offset)
  const {
  // Linear search in the .debug_abbrev section, starting at offset
  folly::StringPiece section = abbrev_;
  section.advance(offset);

  Dwarf::DIEAbbreviation abbr;
  while (readAbbreviation(section, abbr)) {
    if (abbr.code == code) {
      return abbr;
    }
  }

  throw std::runtime_error("could not find abbreviation code");
}

Dwarf::AttributeValue Dwarf::readAttributeValue(
    folly::StringPiece& sp, uint64_t form, bool is64Bit) const {
  switch (form) {
  case DW_FORM_addr:
    return read<uintptr_t>(sp);
  case DW_FORM_block1:
    return readBytes(sp, read<uint8_t>(sp));
  case DW_FORM_block2:
    return readBytes(sp, read<uint16_t>(sp));
  case DW_FORM_block4:
    return readBytes(sp, read<uint32_t>(sp));
  case DW_FORM_block:  // fallthrough
  case DW_FORM_exprloc:
    return readBytes(sp, readULEB(sp));
  case DW_FORM_data1:  // fallthrough
  case DW_FORM_ref1:
    return read<uint8_t>(sp);
  case DW_FORM_data2:  // fallthrough
  case DW_FORM_ref2:
    return read<uint16_t>(sp);
  case DW_FORM_data4:  // fallthrough
  case DW_FORM_ref4:
    return read<uint32_t>(sp);
  case DW_FORM_data8:  // fallthrough
  case DW_FORM_ref8:
    return read<uint64_t>(sp);
  case DW_FORM_sdata:
    return readSLEB(sp);
  case DW_FORM_udata:  // fallthrough
  case DW_FORM_ref_udata:
    return readULEB(sp);
  case DW_FORM_flag:
    return read<uint8_t>(sp);
  case DW_FORM_flag_present:
    return 1;
  case DW_FORM_sec_offset:  // fallthrough
  case DW_FORM_ref_addr:
    return readOffset(sp, is64Bit);
  case DW_FORM_string:
    return readNullTerminated(sp);
  case DW_FORM_strp:
    return getStringFromStringSection(readOffset(sp, is64Bit));
  case DW_FORM_indirect:  // form is explicitly specified
    return readAttributeValue(sp, readULEB(sp), is64Bit);
  default:
    throw std::runtime_error("invalid attribute form");
  }
}

folly::StringPiece Dwarf::getStringFromStringSection(uint64_t offset) const {
  enforce(offset < strings_.size(), "invalid strp offset");
  folly::StringPiece sp(strings_);
  sp.advance(offset);
  return readNullTerminated(sp);
}

bool Dwarf::findAddress(uintptr_t address, LocationInfo& locationInfo) const {
  locationInfo = LocationInfo();

  if (!elf_) {  // no file
    return false;
  }

  // Find address range in .debug_aranges, map to compilation unit
  Section arangesSection(aranges_);
  folly::StringPiece chunk;
  uint64_t debugInfoOffset;
  bool found = false;
  while (!found && arangesSection.next(chunk)) {
    auto version = read<uint16_t>(chunk);
    enforce(version == 2, "invalid aranges version");

    debugInfoOffset = readOffset(chunk, arangesSection.is64Bit());
    auto addressSize = read<uint8_t>(chunk);
    enforce(addressSize == sizeof(uintptr_t), "invalid address size");
    auto segmentSize = read<uint8_t>(chunk);
    enforce(segmentSize == 0, "segmented architecture not supported");

    // Padded to a multiple of 2 addresses.
    // Strangely enough, this is the only place in the DWARF spec that requires
    // padding.
    skipPadding(chunk, aranges_.data(), 2 * sizeof(uintptr_t));
    for (;;) {
      auto start = read<uintptr_t>(chunk);
      auto length = read<uintptr_t>(chunk);

      if (start == 0) {
        break;
      }

      // Is our address in this range?
      if (address >= start && address < start + length) {
        found = true;
        break;
      }
    }
  }

  if (!found) {
    return false;
  }

  // Read compilation unit header from .debug_info
  folly::StringPiece sp(info_);
  sp.advance(debugInfoOffset);
  Section debugInfoSection(sp);
  enforce(debugInfoSection.next(chunk), "invalid debug info");

  auto version = read<uint16_t>(chunk);
  enforce(version >= 2 && version <= 4, "invalid info version");
  uint64_t abbrevOffset = readOffset(chunk, debugInfoSection.is64Bit());
  auto addressSize = read<uint8_t>(chunk);
  enforce(addressSize == sizeof(uintptr_t), "invalid address size");

  // We survived so far.  The first (and only) DIE should be
  // DW_TAG_compile_unit
  // TODO(tudorb): Handle DW_TAG_partial_unit?
  auto code = readULEB(chunk);
  enforce(code != 0, "invalid code");
  auto abbr = getAbbreviation(code, abbrevOffset);
  enforce(abbr.tag == DW_TAG_compile_unit, "expecting compile unit entry");

  // Read attributes, extracting the few we care about
  bool foundLineOffset = false;
  uint64_t lineOffset = 0;
  folly::StringPiece compilationDirectory;
  folly::StringPiece mainFileName;

  DIEAbbreviation::Attribute attr;
  folly::StringPiece attributes = abbr.attributes;
  for (;;) {
    attr = readAttribute(attributes);
    if (attr.name == 0 && attr.form == 0) {
      break;
    }
    auto val = readAttributeValue(chunk, attr.form,
                                  debugInfoSection.is64Bit());
    switch (attr.name) {
    case DW_AT_stmt_list:
      // Offset in .debug_line for the line number VM program for this
      // compilation unit
      lineOffset = boost::get<uint64_t>(val);
      foundLineOffset = true;
      break;
    case DW_AT_comp_dir:
      // Compilation directory
      compilationDirectory = boost::get<folly::StringPiece>(val);
      break;
    case DW_AT_name:
      // File name of main file being compiled
      mainFileName = boost::get<folly::StringPiece>(val);
      break;
    }
  }

  if (!mainFileName.empty()) {
    locationInfo.hasMainFile = true;
    locationInfo.mainFile = Path(compilationDirectory, "", mainFileName);
  }

  if (foundLineOffset) {
    folly::StringPiece lineSection(line_);
    lineSection.advance(lineOffset);
    LineNumberVM lineVM(lineSection, compilationDirectory);

    // Execute line number VM program to find file and line
    locationInfo.hasFileAndLine =
      lineVM.findAddress(address, locationInfo.file, locationInfo.line);
  }

  return true;
}

Dwarf::LineNumberVM::LineNumberVM(folly::StringPiece data,
                                  folly::StringPiece compilationDirectory)
  : compilationDirectory_(compilationDirectory) {
  Section section(data);
  enforce(section.next(data_), "invalid line number VM");
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
  enforce(version_ >= 2 && version_ <= 4, "invalid version in line number VM");
  uint64_t headerLength = readOffset(data_, is64Bit_);
  enforce(headerLength <= data_.size(),
          "invalid line number VM header length");
  folly::StringPiece header(data_.data(), headerLength);
  data_.assign(header.end(), data_.end());

  minLength_ = read<uint8_t>(header);
  if (version_ == 4) {  // Version 2 and 3 records don't have this
    uint8_t maxOpsPerInstruction = read<uint8_t>(header);
    enforce(maxOpsPerInstruction == 1, "VLIW not supported");
  }
  defaultIsStmt_ = read<uint8_t>(header);
  lineBase_ = read<int8_t>(header);  // yes, signed
  lineRange_ = read<uint8_t>(header);
  opcodeBase_ = read<uint8_t>(header);
  enforce(opcodeBase_ != 0, "invalid opcode base");
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

Dwarf::LineNumberVM::FileName Dwarf::LineNumberVM::getFileName(uint64_t index)
  const {
  enforce(index != 0, "invalid file index 0");

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
    enforce(nextDefineFile(program, fn), "invalid file index");
  }

  return fn;
}

folly::StringPiece Dwarf::LineNumberVM::getIncludeDirectory(uint64_t index)
  const {
  if (index == 0) {
    return folly::StringPiece();
  }

  enforce(index <= includeDirectoryCount_, "invalid include directory");

  folly::StringPiece includeDirectories = includeDirectories_;
  folly::StringPiece dir;
  for (; index; --index) {
    dir = readNullTerminated(includeDirectories);
    if (dir.empty()) {
      abort();  // BUG
    }
  }

  return dir;
}

bool Dwarf::LineNumberVM::readFileName(folly::StringPiece& program,
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

bool Dwarf::LineNumberVM::nextDefineFile(folly::StringPiece& program,
                                         FileName& fn) const {
  while (!program.empty()) {
    auto opcode = read<uint8_t>(program);

    if (opcode >= opcodeBase_) {  // special opcode
      continue;
    }

    if (opcode != 0) {  // standard opcode
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
    enforce(length != 0, "invalid extended opcode length");
    read<uint8_t>(program); // extended opcode
    --length;

    if (opcode == DW_LNE_define_file) {
      enforce(readFileName(program, fn),
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

  if (opcode >= opcodeBase_) {  // special opcode
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

  if (opcode != 0) {  // standard opcode
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
      if (version_ == 2) break;  // not supported in version 2
      prologueEnd_ = true;
      return CONTINUE;
    case DW_LNS_set_epilogue_begin:
      if (version_ == 2) break;  // not supported in version 2
      epilogueBegin_ = true;
      return CONTINUE;
    case DW_LNS_set_isa:
      if (version_ == 2) break;  // not supported in version 2
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
  enforce(length != 0, "invalid extende opcode length");
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

bool Dwarf::LineNumberVM::findAddress(uintptr_t target, Path& file,
                                      uint64_t& line) {
  folly::StringPiece program = data_;

  // Within each sequence of instructions, the address may only increase.
  // Unfortunately, within the same compilation unit, sequences may appear
  // in any order.  So any sequence is a candidate if it starts at an address
  // <= the target address, and we know we've found the target address if
  // a candidate crosses the target address.
  enum State {
    START,
    LOW_SEQ,  // candidate
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
        file = Path(compilationDirectory_,
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

}  // namespace symbolizer
}  // namespace folly

