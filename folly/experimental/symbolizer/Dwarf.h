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

// DWARF record parser

#pragma once

#include <boost/variant.hpp>

#include <folly/Function.h>
#include <folly/Range.h>
#include <folly/experimental/symbolizer/Elf.h>
#include <folly/experimental/symbolizer/SymbolizedFrame.h>

namespace folly {
namespace symbolizer {

#if FOLLY_HAVE_DWARF && FOLLY_HAVE_ELF

namespace detail {

// A top level chunk in the .debug_info that contains a compilation unit.
struct CompilationUnit;

// Debugging information entry to define a low-level representation of a
// source program. Each debugging information entry consists of an identifying
// tag and a series of attributes. An entry, or group of entries together,
// provide a description of a corresponding entity in the source program.
struct Die;

// Abbreviation for a Debugging Information Entry.
struct DIEAbbreviation;

struct AttributeSpec;
struct Attribute;
struct CallLocation;

} // namespace detail

/**
 * DWARF record parser.
 *
 * We only implement enough DWARF functionality to convert from PC address
 * to file and line number information.
 *
 * This means (although they're not part of the public API of this class), we
 * can parse Debug Information Entries (DIEs), abbreviations, attributes (of
 * all forms), and we can interpret bytecode for the line number VM.
 *
 * We can interpret DWARF records of version 2, 3, or 4, although we don't
 * actually support many of the version 4 features (such as VLIW, multiple
 * operations per instruction)
 *
 * Note that the DWARF record parser does not allocate heap memory at all.
 * This is on purpose: you can use the parser from
 * memory-constrained situations (such as an exception handler for
 * std::out_of_memory)  If it weren't for this requirement, some things would
 * be much simpler: the Path class would be unnecessary and would be replaced
 * with a std::string; the list of file names in the line number VM would be
 * kept as a vector of strings instead of re-executing the program to look for
 * DW_LNE_define_file instructions, etc.
 */
class Dwarf {
  /**
   * Note that Dwarf uses (and returns) StringPiece a lot.
   * The StringPieces point within sections in the ELF file, and so will
   * be live for as long as the passed-in ElfFile is live.
   */
 public:
  /** Create a DWARF parser around an ELF file. */
  explicit Dwarf(const ElfFile* elf);

  /**
   * More than one location info may exist if current frame is an inline
   * function call.
   */
  static const uint32_t kMaxInlineLocationInfoPerFrame = 10;

  /**
   * Find the file and line number information corresponding to address.
   * If `eachParameterName` is provided, the callback will be invoked once
   * for each parameter of the function.
   */
  bool findAddress(
      uintptr_t address,
      LocationInfoMode mode,
      LocationInfo& info,
      folly::Range<SymbolizedFrame*> inlineFrames = {},
      folly::FunctionRef<void(const folly::StringPiece name)>
          eachParameterName = {}) const;

 private:
  using AttributeValue = boost::variant<uint64_t, folly::StringPiece>;

  /**
   * DWARF section made up of chunks, each prefixed with a length header. The
   * length indicates whether the chunk is DWARF-32 or DWARF-64, which guides
   * interpretation of "section offset" records. (yes, DWARF-32 and DWARF-64
   * sections may coexist in the same file).
   */
  class Section;

  /** Interpreter for the line number bytecode VM */
  class LineNumberVM;

  /**
   * Find the @locationInfo for @address in the compilation unit @cu.
   *
   * Best effort:
   * - fills @inlineFrames if mode == FULL_WITH_INLINE,
   * - calls @eachParameterName on the function parameters.
   */
  bool findLocation(
      uintptr_t address,
      const LocationInfoMode mode,
      detail::CompilationUnit& cu,
      LocationInfo& info,
      folly::Range<SymbolizedFrame*> inlineFrames,
      folly::FunctionRef<void(folly::StringPiece)> eachParameterName) const;

  /**
   * Finds a subprogram debugging info entry that contains a given address among
   * children of given die. Depth first search.
   */
  bool findSubProgramDieForAddress(
      const detail::CompilationUnit& cu,
      const detail::Die& die,
      uint64_t address,
      folly::Optional<uint64_t> baseAddrCU,
      detail::Die& subprogram) const;

  /**
   * Finds inlined subroutine DIEs and their caller lines that contains a given
   * address among children of given die. Depth first search.
   */
  void findInlinedSubroutineDieForAddress(
      const detail::CompilationUnit& cu,
      const detail::Die& die,
      const LineNumberVM& lineVM,
      uint64_t address,
      folly::Optional<uint64_t> baseAddrCU,
      folly::Range<detail::CallLocation*> locations,
      size_t& numFound) const;

  static bool findDebugInfoOffset(
      uintptr_t address, StringPiece aranges, uint64_t& offset);

  /** Get an ELF section by name. */
  folly::StringPiece getSection(const char* name) const;

  /** cu must exist during the life cycle of created detail::Die. */
  detail::Die getDieAtOffset(
      const detail::CompilationUnit& cu, uint64_t offset) const;

  /**
   * Find the actual definition DIE instead of declaration for the given die.
   */
  detail::Die findDefinitionDie(
      const detail::CompilationUnit& cu, const detail::Die& die) const;

  /**
   * Iterates over all children of a debugging info entry, calling the given
   * callable for each. Iteration is stopped early if any of the calls return
   * false. Returns the offset of next DIE after iterations.
   */
  size_t forEachChild(
      const detail::CompilationUnit& cu,
      const detail::Die& die,
      folly::FunctionRef<bool(const detail::Die& die)> f) const;

  /**
   * Gets abbreviation corresponding to a code, in the chunk starting at
   * offset in the .debug_abbrev section
   */
  detail::DIEAbbreviation getAbbreviation(uint64_t code, uint64_t offset) const;

  /**
   * Iterates over all attributes of a debugging info entry,  calling the given
   * callable for each. If all attributes are visited, then return the offset of
   * next DIE, or else iteration is stopped early and return size_t(-1) if any
   * of the calls return false.
   */
  size_t forEachAttribute(
      const detail::CompilationUnit& cu,
      const detail::Die& die,
      folly::FunctionRef<bool(const detail::Attribute& die)> f) const;

  template <class T>
  folly::Optional<T> getAttribute(
      const detail::CompilationUnit& cu,
      const detail::Die& die,
      uint64_t attrName) const;
  /**
   * Check if the given address is in the range list at the given offset in
   * .debug_ranges.
   */
  bool isAddrInRangeList(
      uint64_t address,
      folly::Optional<uint64_t> baseAddr,
      size_t offset,
      uint8_t addrSize) const;

  const ElfFile* elf_;
  const folly::StringPiece debugInfo_; // .debug_info
  const folly::StringPiece debugAbbrev_; // .debug_abbrev
  const folly::StringPiece debugLine_; // .debug_line
  const folly::StringPiece debugStr_; // .debug_str
  const folly::StringPiece debugAranges_; // .debug_aranges
  const folly::StringPiece debugRanges_; // .debug_ranges
};

class Dwarf::Section {
 public:
  Section() : is64Bit_(false) {}

  explicit Section(folly::StringPiece d);

  /**
   * Return next chunk, if any; the 4- or 12-byte length was already
   * parsed and isn't part of the chunk.
   */
  bool next(folly::StringPiece& chunk);

  /** Is the current chunk 64 bit? */
  bool is64Bit() const { return is64Bit_; }

 private:
  // Yes, 32- and 64- bit sections may coexist.  Yikes!
  bool is64Bit_;
  folly::StringPiece data_;
};

class Dwarf::LineNumberVM {
 public:
  LineNumberVM(
      folly::StringPiece data, folly::StringPiece compilationDirectory);

  bool findAddress(uintptr_t target, Path& file, uint64_t& line);

  /** Gets full file name at given index including directory. */
  Path getFullFileName(uint64_t index) const {
    auto fn = getFileName(index);
    return Path({}, getIncludeDirectory(fn.directoryIndex), fn.relativeName);
  }

 private:
  void init();
  void reset();

  /** Execute until we commit one new row to the line number matrix */
  bool next(folly::StringPiece& program);
  enum StepResult {
    CONTINUE, // Continue feeding opcodes
    COMMIT, // Commit new <address, file, line> tuple
    END, // End of sequence
  };
  /** Execute one opcode */
  StepResult step(folly::StringPiece& program);

  struct FileName {
    folly::StringPiece relativeName;
    // 0 = current compilation directory
    // otherwise, 1-based index in the list of include directories
    uint64_t directoryIndex;
  };

  /** Read one FileName object, advance sp */
  static bool readFileName(folly::StringPiece& program, FileName& fn);

  /**
   * Get file name at given index; may be in the initial table
   * (fileNames_) or defined using DW_LNE_define_file (and we reexecute
   * enough of the program to find it, if so)
   */
  FileName getFileName(uint64_t index) const;

  /** Get include directory at given index */
  folly::StringPiece getIncludeDirectory(uint64_t index) const;

  /**
   * Execute opcodes until finding a DW_LNE_define_file and return true;
   * return file at the end.
   */
  bool nextDefineFile(folly::StringPiece& program, FileName& fn) const;

  // Initialization
  bool is64Bit_;
  folly::StringPiece data_;
  folly::StringPiece compilationDirectory_;

  // Header
  uint16_t version_;
  uint8_t minLength_;
  bool defaultIsStmt_;
  int8_t lineBase_;
  uint8_t lineRange_;
  uint8_t opcodeBase_;
  const uint8_t* standardOpcodeLengths_;

  folly::StringPiece includeDirectories_;
  size_t includeDirectoryCount_;

  folly::StringPiece fileNames_;
  size_t fileNameCount_;

  // State machine registers
  uint64_t address_;
  uint64_t file_;
  uint64_t line_;
  uint64_t column_;
  bool isStmt_;
  bool basicBlock_;
  bool endSequence_;
  bool prologueEnd_;
  bool epilogueBegin_;
  uint64_t isa_;
  uint64_t discriminator_;
};

#endif

} // namespace symbolizer
} // namespace folly
