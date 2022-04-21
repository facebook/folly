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

// ELF file parser

#pragma once
#define FOLLY_EXPERIMENTAL_SYMBOLIZER_ELF_H_

#include <fcntl.h>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <system_error>

#include <folly/Conv.h>
#include <folly/Likely.h>
#include <folly/Range.h>
#include <folly/lang/SafeAssert.h>
#include <folly/portability/Config.h>

#if FOLLY_HAVE_ELF

#include <elf.h>
#include <link.h> // For ElfW()

namespace folly {
namespace symbolizer {

#if defined(ElfW)
#define FOLLY_ELF_ELFW(name) ElfW(name)
#elif defined(__FreeBSD__)
#define FOLLY_ELF_ELFW(name) Elf_##name
#endif

using ElfAddr = FOLLY_ELF_ELFW(Addr);
using ElfEhdr = FOLLY_ELF_ELFW(Ehdr);
using ElfOff = FOLLY_ELF_ELFW(Off);
using ElfPhdr = FOLLY_ELF_ELFW(Phdr);
using ElfShdr = FOLLY_ELF_ELFW(Shdr);
using ElfSym = FOLLY_ELF_ELFW(Sym);

/**
 * ELF file parser.
 *
 * We handle native files only (32-bit files on a 32-bit platform, 64-bit files
 * on a 64-bit platform), and only executables (ET_EXEC) shared objects
 * (ET_DYN), core files (ET_CORE) and relocatable file (ET_REL).
 */
class ElfFile {
 public:
  class Options {
   public:
    constexpr Options() noexcept {}

    constexpr bool writable() const noexcept { return writable_; }

    constexpr Options& writable(bool const value) noexcept {
      writable_ = value;
      return *this;
    }

   private:
    bool writable_ = false;
  };

  ElfFile() noexcept;

  // Note: may throw, call openNoThrow() explicitly if you don't want to throw
  explicit ElfFile(const char* name, Options const& options = Options());

  // Open the ELF file.
  // Returns 0 on success, kSystemError (guaranteed to be -1) (and sets errno)
  // on IO error, kInvalidElfFile (and sets errno to EINVAL) for an invalid
  // Elf file. On error, if msg is not nullptr, sets *msg to a static string
  // indicating what failed.
  enum OpenResultCode : int {
    kSuccess = 0,
    kSystemError = -1,
    kInvalidElfFile = -2,
  };
  struct OpenResult {
    OpenResultCode code{};
    char const* msg{};

    /* implicit */ constexpr operator OpenResultCode() const noexcept {
      return code;
    }
  };
  // Open the ELF file. Does not throw on error.
  OpenResult openNoThrow(
      const char* name, Options const& options = Options()) noexcept;

  // Like openNoThrow, but follow .gnu_debuglink if present
  OpenResult openAndFollow(
      const char* name, Options const& options = Options()) noexcept;

  // Open the ELF file. Throws on error.
  void open(const char* name, Options const& options = Options());

  ~ElfFile();

  ElfFile(ElfFile&& other) noexcept;
  ElfFile& operator=(ElfFile&& other) noexcept;

  /** Retrieve the ELF header */
  const ElfEhdr& elfHeader() const noexcept { return at<ElfEhdr>(0); }

  /**
   * Get the base address, the address where the file should be loaded if
   * no relocations happened.
   */
  uintptr_t getBaseAddress() const noexcept { return baseAddress_; }

  /** Find a section given its name */
  const ElfShdr* getSectionByName(const char* name) const noexcept;

  /** Find a section given its index in the section header table */
  const ElfShdr* getSectionByIndex(size_t idx) const noexcept;

  /** Retrieve the name of a section */
  const char* getSectionName(const ElfShdr& section) const noexcept;

  /** Get the actual section body */
  folly::StringPiece getSectionBody(const ElfShdr& section) const noexcept;

  /** Retrieve a string from a string table section */
  const char* getString(
      const ElfShdr& stringTable, size_t offset) const noexcept;

  /**
   * Iterate over all strings in a string table section for as long as
   * fn(str) returns false.
   * Returns the current ("found") string when fn returned true, or nullptr
   * if fn returned false for all strings in the table.
   */
  template <class Fn>
  const char* iterateStrings(const ElfShdr& stringTable, Fn fn) const
      noexcept(is_nothrow_invocable_v<Fn, const char*>);

  /**
   * Iterate over program headers as long as fn(section) returns false.
   * Returns a pointer to the current ("found") section when fn returned
   * true, or nullptr if fn returned false for all sections.
   */
  template <class Fn>
  const ElfPhdr* iterateProgramHeaders(Fn fn) const
      noexcept(is_nothrow_invocable_v<Fn, ElfPhdr const&>);

  /**
   * Iterate over all sections for as long as fn(section) returns false.
   * Returns a pointer to the current ("found") section when fn returned
   * true, or nullptr if fn returned false for all sections.
   */
  template <class Fn>
  const ElfShdr* iterateSections(Fn fn) const
      noexcept(is_nothrow_invocable_v<Fn, ElfShdr const&>);

  /**
   * Iterate over all sections with a given type. Similar to
   * iterateSections(), but filtered only for sections with the given type.
   */
  template <class Fn>
  const ElfShdr* iterateSectionsWithType(uint32_t type, Fn fn) const
      noexcept(is_nothrow_invocable_v<Fn, ElfShdr const&>);

  /**
   * Iterate over all sections with a given types. Similar to
   * iterateSectionWithTypes(), but filtered on multiple types.
   */
  template <class Fn>
  const ElfShdr* iterateSectionsWithTypes(
      std::initializer_list<uint32_t> types, Fn fn) const
      noexcept(is_nothrow_invocable_v<Fn, ElfShdr const&>);

  /**
   * Iterate over all symbols witin a given section.
   *
   * Returns a pointer to the current ("found") symbol when fn returned true,
   * or nullptr if fn returned false for all symbols.
   */
  template <class Fn>
  const ElfSym* iterateSymbols(const ElfShdr& section, Fn fn) const
      noexcept(is_nothrow_invocable_v<Fn, ElfSym const&>);
  template <class Fn>
  const ElfSym* iterateSymbolsWithType(
      const ElfShdr& section, uint32_t type, Fn fn) const
      noexcept(is_nothrow_invocable_v<Fn, ElfSym const&>);
  template <class Fn>
  const ElfSym* iterateSymbolsWithTypes(
      const ElfShdr& section,
      std::initializer_list<uint32_t> types,
      Fn fn) const noexcept(is_nothrow_invocable_v<Fn, ElfSym const&>);

  /**
   * Find symbol definition by address.
   * Note that this is the file virtual address, so you need to undo
   * any relocation that might have happened.
   *
   * Returns {nullptr, nullptr} if not found.
   */
  typedef std::pair<const ElfShdr*, const ElfSym*> Symbol;
  Symbol getDefinitionByAddress(uintptr_t address) const noexcept;

  /**
   * Find symbol definition by name.
   *
   * If a symbol with this name cannot be found, a <nullptr, nullptr> Symbol
   * will be returned. This is O(N) in the number of symbols in the file.
   *
   * Returns {nullptr, nullptr} if not found.
   */
  Symbol getSymbolByName(const char* name) const noexcept;

  /**
   * Get the value of a symbol.
   */
  template <class T>
  const T& getSymbolValue(const ElfSym* symbol) const noexcept {
    const ElfShdr* section = getSectionByIndex(symbol->st_shndx);
    FOLLY_SAFE_CHECK(section, "Symbol's section index is invalid");

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
   *  auto addr = getSymbolValue<ElfAddr>(sym.second);
   *  const char* str = &getAddressValue<const char>(addr);
   */
  template <class T>
  const T& getAddressValue(const ElfAddr addr) const noexcept {
    const ElfShdr* section = getSectionContainingAddress(addr);
    FOLLY_SAFE_CHECK(section, "Address does not refer to existing section");

    return valueAt<T>(*section, addr);
  }

  /**
   * Retrieve symbol name.
   */
  const char* getSymbolName(Symbol symbol) const noexcept;

  /** Find the section containing the given address */
  const ElfShdr* getSectionContainingAddress(ElfAddr addr) const noexcept;

  const char* filepath() const { return filepath_; }

  /**
   * Announce an intention to access file data in a specific pattern in the
   * future. https://man7.org/linux/man-pages/man2/posix_fadvise.2.html
   */
  std::pair<const int, char const*> posixFadvise(
      off_t offset, off_t len, int const advice) const noexcept;
  std::pair<const int, char const*> posixFadvise(
      int const advice) const noexcept;

 private:
  OpenResult init() noexcept;
  void reset() noexcept;
  ElfFile(const ElfFile&) = delete;
  ElfFile& operator=(const ElfFile&) = delete;

  void validateStringTable(const ElfShdr& stringTable) const noexcept;

  template <class T>
  const T& at(ElfOff offset) const noexcept {
    static_assert(
        std::is_standard_layout<T>::value && std::is_trivial<T>::value,
        "non-pod");
    FOLLY_SAFE_CHECK(
        offset + sizeof(T) <= length_,
        "Offset (",
        static_cast<size_t>(offset),
        " + ",
        sizeof(T),
        ") is not contained within our mapped file (",
        filepath_,
        ") of length ",
        length_);
    return *reinterpret_cast<T*>(file_ + offset);
  }

  template <class T>
  const T& valueAt(const ElfShdr& section, const ElfAddr addr) const noexcept {
    // For exectuables and shared objects, st_value holds a virtual address
    // that refers to the memory owned by sections. Since we didn't map the
    // sections into the addresses that they're expecting (sh_addr), but
    // instead just mmapped the entire file directly, we need to translate
    // between addresses and offsets into the file.
    //
    // TODO: For other file types, st_value holds a file offset directly. Since
    //       I don't have a use-case for that right now, just assert that
    //       nobody wants this. We can always add it later.
    FOLLY_SAFE_CHECK(
        elfHeader().e_type == ET_EXEC || elfHeader().e_type == ET_DYN ||
            elfHeader().e_type == ET_CORE,
        "Only exectuables, shared objects and cores are supported");
    FOLLY_SAFE_CHECK(
        addr >= section.sh_addr &&
            (addr + sizeof(T)) <= (section.sh_addr + section.sh_size),
        "Address is not contained within the provided segment");

    return at<T>(section.sh_offset + (addr - section.sh_addr));
  }

  static constexpr size_t kFilepathMaxLen = 512;
  char filepath_[kFilepathMaxLen] = {};
  int fd_;
  char* file_; // mmap() location
  size_t length_; // mmap() length

  uintptr_t baseAddress_;
};

} // namespace symbolizer
} // namespace folly

#include <folly/experimental/symbolizer/Elf-inl.h>

#endif // FOLLY_HAVE_ELF
