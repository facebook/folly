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

#pragma once

#include <folly/Range.h>
#include <folly/experimental/symbolizer/DwarfUtil.h>
#include <folly/experimental/symbolizer/SymbolizedFrame.h>

namespace folly {
namespace symbolizer {

#if FOLLY_HAVE_DWARF && FOLLY_HAVE_ELF

class DwarfLineNumberVM {
 public:
  DwarfLineNumberVM(
      folly::StringPiece data,
      folly::StringPiece compilationDirectory,
      const DebugSections& debugSections);

  bool findAddress(uintptr_t target, Path& file, uint64_t& line);

  /** Gets full file name at given index including directory. */
  Path getFullFileName(uint64_t index) const;

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
  const DebugSections& debugSections_;

  // Header
  uint16_t version_;
  uint8_t minLength_;
  bool defaultIsStmt_;
  int8_t lineBase_;
  uint8_t lineRange_;
  uint8_t opcodeBase_;
  const uint8_t* standardOpcodeLengths_;

  // 6.2.4 The Line Number Program Header.
  struct {
    size_t includeDirectoryCount;
    folly::StringPiece includeDirectories;
    size_t fileNameCount;
    folly::StringPiece fileNames;
  } v4_;

  struct {
    uint8_t directoryEntryFormatCount;
    folly::StringPiece directoryEntryFormat;
    uint64_t directoriesCount;
    folly::StringPiece directories;

    uint8_t fileNameEntryFormatCount;
    folly::StringPiece fileNameEntryFormat;
    uint64_t fileNamesCount;
    folly::StringPiece fileNames;
  } v5_;

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
