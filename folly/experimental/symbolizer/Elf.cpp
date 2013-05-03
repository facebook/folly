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


#include "folly/experimental/symbolizer/Elf.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string>

#include <glog/logging.h>

#include "folly/Conv.h"
#include "folly/Exception.h"

namespace folly {
namespace symbolizer {

ElfFile::ElfFile()
  : fd_(-1),
    file_(static_cast<char*>(MAP_FAILED)),
    length_(0),
    baseAddress_(0) {
}

ElfFile::ElfFile(const char* name, bool readOnly)
  : fd_(open(name, (readOnly) ? O_RDONLY : O_RDWR)),
    file_(static_cast<char*>(MAP_FAILED)),
    length_(0),
    baseAddress_(0) {
  if (fd_ == -1) {
    folly::throwSystemError("open ", name);
  }

  struct stat st;
  int r = fstat(fd_, &st);
  if (r == -1) {
    folly::throwSystemError("fstat");
  }

  length_ = st.st_size;
  int prot = PROT_READ;
  if (!readOnly) {
    prot |= PROT_WRITE;
  }
  file_ = static_cast<char*>(mmap(nullptr, length_, prot, MAP_SHARED, fd_, 0));
  if (file_ == MAP_FAILED) {
    folly::throwSystemError("mmap");
  }
  init();
}

ElfFile::~ElfFile() {
  destroy();
}

ElfFile::ElfFile(ElfFile&& other)
  : fd_(other.fd_),
    file_(other.file_),
    length_(other.length_),
    baseAddress_(other.baseAddress_) {
  other.fd_ = -1;
  other.file_ = static_cast<char*>(MAP_FAILED);
  other.length_ = 0;
  other.baseAddress_ = 0;
}

ElfFile& ElfFile::operator=(ElfFile&& other) {
  assert(this != &other);
  destroy();

  fd_ = other.fd_;
  file_ = other.file_;
  length_ = other.length_;
  baseAddress_ = other.baseAddress_;

  other.fd_ = -1;
  other.file_ = static_cast<char*>(MAP_FAILED);
  other.length_ = 0;
  other.baseAddress_ = 0;

  return *this;
}

void ElfFile::destroy() {
  if (file_ != MAP_FAILED) {
    munmap(file_, length_);
  }

  if (fd_ != -1) {
    close(fd_);
  }
}

void ElfFile::init() {
  auto& elfHeader = this->elfHeader();

  // Validate ELF magic numbers
  enforce(elfHeader.e_ident[EI_MAG0] == ELFMAG0 &&
          elfHeader.e_ident[EI_MAG1] == ELFMAG1 &&
          elfHeader.e_ident[EI_MAG2] == ELFMAG2 &&
          elfHeader.e_ident[EI_MAG3] == ELFMAG3,
          "invalid ELF magic");

  // Validate ELF class (32/64 bits)
#define EXPECTED_CLASS P1(ELFCLASS, __ELF_NATIVE_CLASS)
#define P1(a, b) P2(a, b)
#define P2(a, b) a ## b
  enforce(elfHeader.e_ident[EI_CLASS] == EXPECTED_CLASS,
          "invalid ELF class");
#undef P1
#undef P2
#undef EXPECTED_CLASS

  // Validate ELF data encoding (LSB/MSB)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define EXPECTED_ENCODING ELFDATA2LSB
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define EXPECTED_ENCODING ELFDATA2MSB
#else
# error Unsupported byte order
#endif
  enforce(elfHeader.e_ident[EI_DATA] == EXPECTED_ENCODING,
          "invalid ELF encoding");
#undef EXPECTED_ENCODING

  // Validate ELF version (1)
  enforce(elfHeader.e_ident[EI_VERSION] == EV_CURRENT &&
          elfHeader.e_version == EV_CURRENT,
          "invalid ELF version");

  // We only support executable and shared object files
  enforce(elfHeader.e_type == ET_EXEC || elfHeader.e_type == ET_DYN,
          "invalid ELF file type");

  enforce(elfHeader.e_phnum != 0, "no program header!");
  enforce(elfHeader.e_phentsize == sizeof(ElfW(Phdr)),
          "invalid program header entry size");
  enforce(elfHeader.e_shentsize == sizeof(ElfW(Shdr)),
          "invalid section header entry size");

  const ElfW(Phdr)* programHeader = &at<ElfW(Phdr)>(elfHeader.e_phoff);
  bool foundBase = false;
  for (size_t i = 0; i < elfHeader.e_phnum; programHeader++, i++) {
    // Program headers are sorted by load address, so the first PT_LOAD
    // header gives us the base address.
    if (programHeader->p_type == PT_LOAD) {
      baseAddress_ = programHeader->p_vaddr;
      foundBase = true;
      break;
    }
  }

  enforce(foundBase, "could not find base address");
}

const ElfW(Shdr)* ElfFile::getSectionByIndex(size_t idx) const {
  enforce(idx < elfHeader().e_shnum, "invalid section index");
  return &at<ElfW(Shdr)>(elfHeader().e_shoff + idx * sizeof(ElfW(Shdr)));
}

folly::StringPiece ElfFile::getSectionBody(const ElfW(Shdr)& section) const {
  return folly::StringPiece(file_ + section.sh_offset, section.sh_size);
}

void ElfFile::validateStringTable(const ElfW(Shdr)& stringTable) const {
  enforce(stringTable.sh_type == SHT_STRTAB, "invalid type for string table");

  const char* start = file_ + stringTable.sh_offset;
  // First and last bytes must be 0
  enforce(stringTable.sh_size == 0 ||
          (start[0] == '\0' && start[stringTable.sh_size - 1] == '\0'),
          "invalid string table");
}

const char* ElfFile::getString(const ElfW(Shdr)& stringTable, size_t offset)
  const {
  validateStringTable(stringTable);
  enforce(offset < stringTable.sh_size, "invalid offset in string table");

  return file_ + stringTable.sh_offset + offset;
}

const char* ElfFile::getSectionName(const ElfW(Shdr)& section) const {
  if (elfHeader().e_shstrndx == SHN_UNDEF) {
    return nullptr;  // no section name string table
  }

  const ElfW(Shdr)& sectionNames = *getSectionByIndex(elfHeader().e_shstrndx);
  return getString(sectionNames, section.sh_name);
}

const ElfW(Shdr)* ElfFile::getSectionByName(const char* name) const {
  if (elfHeader().e_shstrndx == SHN_UNDEF) {
    return nullptr;  // no section name string table
  }

  // Find offset in the section name string table of the requested name
  const ElfW(Shdr)& sectionNames = *getSectionByIndex(elfHeader().e_shstrndx);
  const char* foundName = iterateStrings(
      sectionNames,
      [&] (const char* s) { return !strcmp(name, s); });
  if (foundName == nullptr) {
    return nullptr;
  }

  size_t offset = foundName - (file_ + sectionNames.sh_offset);

  // Find section with the appropriate sh_name offset
  const ElfW(Shdr)* foundSection = iterateSections(
    [&](const ElfW(Shdr)& sh) {
      if (sh.sh_name == offset) {
        return true;
      }
      return false;
    });
  return foundSection;
}

ElfFile::Symbol ElfFile::getDefinitionByAddress(uintptr_t address) const {
  Symbol foundSymbol {nullptr, nullptr};

  auto findSection = [&](const ElfW(Shdr)& section) {
    auto findSymbols = [&](const ElfW(Sym)& sym) {
      if (sym.st_shndx == SHN_UNDEF) {
        return false;  // not a definition
      }
      if (address >= sym.st_value && address < sym.st_value + sym.st_size) {
        foundSymbol.first = &section;
        foundSymbol.second = &sym;
        return true;
      }

      return false;
    };

    return iterateSymbolsWithType(section, STT_OBJECT, findSymbols) ||
      iterateSymbolsWithType(section, STT_FUNC, findSymbols);
  };

  // Try the .dynsym section first if it exists, it's smaller.
  (iterateSectionsWithType(SHT_DYNSYM, findSection) ||
   iterateSectionsWithType(SHT_SYMTAB, findSection));

  return foundSymbol;
}

ElfFile::Symbol ElfFile::getSymbolByName(const char* name) const {
  Symbol foundSymbol{nullptr, nullptr};

  auto findSection = [&](const ElfW(Shdr)& section) -> bool {
    // This section has no string table associated w/ its symbols; hence we
    // can't get names for them
    if (section.sh_link == SHN_UNDEF) {
      return false;
    }

    auto findSymbols = [&](const ElfW(Sym)& sym) -> bool {
      if (sym.st_shndx == SHN_UNDEF) {
        return false;  // not a definition
      }
      if (sym.st_name == 0) {
        return false;  // no name for this symbol
      }
      const char* sym_name = getString(
        *getSectionByIndex(section.sh_link), sym.st_name);
      if (strcmp(sym_name, name) == 0) {
        foundSymbol.first = &section;
        foundSymbol.second = &sym;
        return true;
      }

      return false;
    };

    return iterateSymbolsWithType(section, STT_OBJECT, findSymbols) ||
      iterateSymbolsWithType(section, STT_FUNC, findSymbols);
  };

  // Try the .dynsym section first if it exists, it's smaller.
  iterateSectionsWithType(SHT_DYNSYM, findSection) ||
    iterateSectionsWithType(SHT_SYMTAB, findSection);

  return foundSymbol;
}

const ElfW(Shdr)* ElfFile::getSectionContainingAddress(ElfW(Addr) addr) const {
  return iterateSections([&](const ElfW(Shdr)& sh) -> bool {
    return (addr >= sh.sh_addr) && (addr < (sh.sh_addr + sh.sh_size));
  });
}

const char* ElfFile::getSymbolName(Symbol symbol) const {
  if (!symbol.first || !symbol.second) {
    return nullptr;
  }

  if (symbol.second->st_name == 0) {
    return nullptr;  // symbol has no name
  }

  if (symbol.first->sh_link == SHN_UNDEF) {
    return nullptr;  // symbol table has no strings
  }

  return getString(*getSectionByIndex(symbol.first->sh_link),
                   symbol.second->st_name);
}

}  // namespace symbolizer
}  // namespace folly

