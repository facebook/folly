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

// ELF file parser

#ifndef FOLLY_EXPERIMENTAL_SYMBOLIZER_ELF_H_
#define FOLLY_EXPERIMENTAL_SYMBOLIZER_ELF_H_

#include <stdio.h>
#include <elf.h>
#include <link.h>  // For ElfW()

#include <stdexcept>
#include <system_error>

#include "folly/Likely.h"
#include "folly/Range.h"
#include "folly/Conv.h"

namespace folly {
namespace symbolizer {

template <class... Args>
inline void enforce(bool v, Args... args) {
  if (UNLIKELY(!v)) {
    throw std::runtime_error(folly::to<std::string>(args...));
  }
}

/**
 * ELF file parser.
 *
 * We handle native files only (32-bit files on a 32-bit platform, 64-bit files
 * on a 64-bit platform), and only executables (ET_EXEC) and shared objects
 * (ET_DYN).
 */
class ElfFile {
 public:
  ElfFile();
  explicit ElfFile(const char* name, bool readOnly=true);
  ~ElfFile();

  ElfFile(ElfFile&& other);
  ElfFile& operator=(ElfFile&& other);

  /** Retrieve the ELF header */
  const ElfW(Ehdr)& elfHeader() const {
    return at<ElfW(Ehdr)>(0);
  }

  /**
   * Get the base address, the address where the file should be loaded if
   * no relocations happened.
   */
  uintptr_t getBaseAddress() const {
    return baseAddress_;
  }

  /** Find a section given its name */
  const ElfW(Shdr)* getSectionByName(const char* name) const;

  /** Find a section given its index in the section header table */
  const ElfW(Shdr)* getSectionByIndex(size_t idx) const;

  /** Retrieve the name of a section */
  const char* getSectionName(const ElfW(Shdr)& section) const;

  /** Get the actual section body */
  folly::StringPiece getSectionBody(const ElfW(Shdr)& section) const;

  /** Retrieve a string from a string table section */
  const char* getString(const ElfW(Shdr)& stringTable, size_t offset) const;

  /**
   * Iterate over all strings in a string table section for as long as
   * fn(str) returns false.
   * Returns the current ("found") string when fn returned true, or nullptr
   * if fn returned false for all strings in the table.
   */
  template <class Fn>
  const char* iterateStrings(const ElfW(Shdr)& stringTable, Fn fn) const;

  /**
   * Iterate over all sections for as long as fn(section) returns false.
   * Returns a pointer to the current ("found") section when fn returned
   * true, or nullptr if fn returned false for all sections.
   */
  template <class Fn>
  const ElfW(Shdr)* iterateSections(Fn fn) const;

  /**
   * Iterate over all sections with a given type.  Similar to
   * iterateSections(), but filtered only for sections with the given type.
   */
  template <class Fn>
  const ElfW(Shdr)* iterateSectionsWithType(uint32_t type, Fn fn) const;

  /**
   * Iterate over all symbols witin a given section.
   *
   * Returns a pointer to the current ("found") symbol when fn returned true,
   * or nullptr if fn returned false for all symbols.
   */
  template <class Fn>
  const ElfW(Sym)* iterateSymbols(const ElfW(Shdr)& section, Fn fn) const;
  template <class Fn>
  const ElfW(Sym)* iterateSymbolsWithType(const ElfW(Shdr)& section,
                                          uint32_t type, Fn fn) const;

  /**
   * Find symbol definition by address.
   * Note that this is the file virtual address, so you need to undo
   * any relocation that might have happened.
   */
  typedef std::pair<const ElfW(Shdr)*, const ElfW(Sym)*> Symbol;
  Symbol getDefinitionByAddress(uintptr_t address) const;

  /**
   * Find symbol definition by name.
   *
   * If a symbol with this name cannot be found, a <nullptr, nullptr> Symbol
   * will be returned. This is O(N) in the number of symbols in the file.
   */
  Symbol getSymbolByName(const char* name) const;

  /**
   * Get the value of a symbol.
   */
  template <class T>
  const T& getSymbolValue(const ElfW(Sym)* symbol) const {
    const ElfW(Shdr)* section = getSectionByIndex(symbol->st_shndx);
    enforce(section, "Symbol's section index is invalid");

    return valueAt<T>(*section, symbol->st_value);
  }

  /**
   * Get the value of the object stored at the given address.
   *
   * This is the function that you want to use in conjunction with
   * getSymbolValue() to follow pointers. For example, to get the value of
   * a char* symbol, you'd do something like this:
   *
   *  auto sym = getSymbolByName("someGlobalValue");
   *  auto addr = getSymbolValue<ElfW(Addr)>(sym.second);
   *  const char* str = &getSymbolValue<const char>(addr);
   */
  template <class T>
  const T& getAddressValue(const ElfW(Addr) addr) const {
    const ElfW(Shdr)* section = getSectionContainingAddress(addr);
    enforce(section, "Address does not refer to existing section");

    return valueAt<T>(*section, addr);
  }

  /**
   * Retrieve symbol name.
   */
  const char* getSymbolName(Symbol symbol) const;

  /** Find the section containing the given address */
  const ElfW(Shdr)* getSectionContainingAddress(ElfW(Addr) addr) const;

 private:
  void init();
  void destroy();
  ElfFile(const ElfFile&) = delete;
  ElfFile& operator=(const ElfFile&) = delete;

  void validateStringTable(const ElfW(Shdr)& stringTable) const;

  template <class T>
  const typename std::enable_if<std::is_pod<T>::value, T>::type&
  at(ElfW(Off) offset) const {
    enforce(offset + sizeof(T) <= length_,
            "Offset is not contained within our mmapped file");

    return *reinterpret_cast<T*>(file_ + offset);
  }

  template <class T>
  const T& valueAt(const ElfW(Shdr)& section, const ElfW(Addr) addr) const {
    // For exectuables and shared objects, st_value holds a virtual address
    // that refers to the memory owned by sections. Since we didn't map the
    // sections into the addresses that they're expecting (sh_addr), but
    // instead just mmapped the entire file directly, we need to translate
    // between addresses and offsets into the file.
    //
    // TODO: For other file types, st_value holds a file offset directly. Since
    //       I don't have a use-case for that right now, just assert that
    //       nobody wants this. We can always add it later.
    enforce(elfHeader().e_type == ET_EXEC || elfHeader().e_type == ET_DYN,
            "Only exectuables and shared objects are supported");
    enforce(addr >= section.sh_addr &&
            (addr + sizeof(T)) <= (section.sh_addr + section.sh_size),
            "Address is not contained within the provided segment");

    return at<T>(section.sh_offset + (addr - section.sh_addr));
  }

  int fd_;
  char* file_;     // mmap() location
  size_t length_;  // mmap() length

  uintptr_t baseAddress_;
};

}  // namespace symbolizer
}  // namespace folly

#include "folly/experimental/symbolizer/Elf-inl.h"

#endif /* FOLLY_EXPERIMENTAL_SYMBOLIZER_ELF_H_ */

