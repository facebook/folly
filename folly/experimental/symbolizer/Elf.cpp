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

#include <folly/experimental/symbolizer/Elf.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <string>

#include <glog/logging.h>

#include <folly/Conv.h>
#include <folly/Exception.h>
#include <folly/ScopeGuard.h>
#include <folly/lang/CString.h>
#include <folly/portability/Config.h>
#include <folly/portability/SysMman.h>

#if FOLLY_HAVE_ELF

#ifndef STT_GNU_IFUNC
#define STT_GNU_IFUNC 10
#endif

#if defined(__ELF_NATIVE_CLASS)
#define FOLLY_ELF_NATIVE_CLASS __ELF_NATIVE_CLASS
#elif defined(__FreeBSD__)
#if defined(__LP64__)
#define FOLLY_ELF_NATIVE_CLASS 64
#else
#define FOLLY_ELF_NATIVE_CLASS 32
#endif
#elif defined(__ANDROID__)
#define FOLLY_ELF_NATIVE_CLASS __WORDSIZE
#endif // __ELF_NATIVE_CLASS

namespace folly {
namespace symbolizer {

ElfFile::ElfFile() noexcept
    : fd_(-1),
      file_(static_cast<char*>(MAP_FAILED)),
      length_(0),
      fileId_(),
      baseAddress_(0) {}

ElfFile::ElfFile(const char* name, Options const& options)
    : fd_(-1),
      file_(static_cast<char*>(MAP_FAILED)),
      length_(0),
      fileId_(),
      baseAddress_(0) {
  open(name, options);
}

void ElfFile::open(const char* name, Options const& options) {
  auto r = openNoThrow(name, options);
  if (r == kSystemError) {
    throwSystemError(r.msg);
  } else {
    CHECK_EQ(r, kSuccess) << r.msg;
  }
}

ElfFile::OpenResult ElfFile::openNoThrow(
    const char* name, Options const& options) noexcept {
  FOLLY_SAFE_CHECK(fd_ == -1, "File already open");
  // Always close fd and unmap in case of failure along the way to avoid
  // check failure above if we leave fd != -1 and the object is recycled
  auto guard = makeGuard([&] { reset(); });
  strlcpy(filepath_, name, kFilepathMaxLen - 1);
  fd_ = ::open(name, options.writable() ? O_RDWR : O_RDONLY);
  if (fd_ == -1) {
    return {kSystemError, "open"};
  }
  struct stat st;
  int r = fstat(fd_, &st);
  if (r == -1) {
    return {kSystemError, "fstat"};
  }
  if (st.st_blocks == 0) {
    return {kSystemError, "unalloc"};
  }

  uint64_t mtime_ns = st.st_mtim.tv_sec * 1000'000'000LL + st.st_mtim.tv_nsec;
  fileId_ = ElfFileId{st.st_dev, st.st_ino, st.st_size, mtime_ns};

  length_ = st.st_size;
  int prot = PROT_READ;
  if (options.writable()) {
    prot |= PROT_WRITE;
  }
  file_ = static_cast<char*>(mmap(nullptr, length_, prot, MAP_SHARED, fd_, 0));
  if (file_ == MAP_FAILED) {
    return {kSystemError, "mmap"};
  }
  auto const initOpenResult = init();
  if (initOpenResult != kSuccess) {
    reset();
    errno = EINVAL;
    return initOpenResult;
  }
  guard.dismiss();
  return {kSuccess, nullptr};
}

ElfFile::OpenResult ElfFile::openAndFollow(
    const char* name, Options const& options) noexcept {
  auto result = openNoThrow(name, options);
  if (options.writable() || result != kSuccess) {
    return result;
  }

  /* NOTE .gnu_debuglink specifies only the name of the debugging info file
   * (with no directory components). GDB checks 3 different directories, but
   * ElfFile only supports the first version:
   *     - dirname(name)
   *     - dirname(name) + /.debug/
   *     - X/dirname(name)/ - where X is set in gdb's `debug-file-directory`.
   */
  auto dirend = strrchr(name, '/');
  // include ending '/' if any.
  auto dirlen = dirend != nullptr ? dirend + 1 - name : 0;

  auto debuginfo = getSectionByName(".gnu_debuglink");
  if (!debuginfo) {
    return result;
  }

  // The section starts with the filename, with any leading directory
  // components removed, followed by a zero byte.
  auto debugFileName = getSectionBody(*debuginfo);
  auto debugFileLen = strlen(debugFileName.begin());
  if (dirlen + debugFileLen >= PATH_MAX) {
    return result;
  }

  char linkname[PATH_MAX];
  memcpy(linkname, name, dirlen);
  memcpy(linkname + dirlen, debugFileName.begin(), debugFileLen + 1);
  reset();
  result = openNoThrow(linkname, options);
  if (result == kSuccess) {
    return result;
  }
  return openNoThrow(name, options);
}

ElfFile::~ElfFile() {
  reset();
}

ElfFile::ElfFile(ElfFile&& other) noexcept
    : fd_(other.fd_),
      file_(other.file_),
      length_(other.length_),
      fileId_(other.fileId_),
      baseAddress_(other.baseAddress_) {
  // copy other.filepath_, leaving filepath_ zero-terminated, always.
  strlcpy(filepath_, other.filepath_, kFilepathMaxLen - 1);
  other.filepath_[0] = 0;
  other.fd_ = -1;
  other.file_ = static_cast<char*>(MAP_FAILED);
  other.length_ = 0;
  other.fileId_ = {};
  other.baseAddress_ = 0;
}

ElfFile& ElfFile::operator=(ElfFile&& other) noexcept {
  assert(this != &other);
  reset();

  // copy other.filepath_, leaving filepath_ zero-terminated, always.
  strlcpy(filepath_, other.filepath_, kFilepathMaxLen - 1);
  fd_ = other.fd_;
  file_ = other.file_;
  length_ = other.length_;
  fileId_ = other.fileId_;
  baseAddress_ = other.baseAddress_;

  other.filepath_[0] = 0;
  other.fd_ = -1;
  other.file_ = static_cast<char*>(MAP_FAILED);
  other.length_ = 0;
  other.fileId_ = {};
  other.baseAddress_ = 0;

  return *this;
}

void ElfFile::reset() noexcept {
  filepath_[0] = 0;

  if (file_ != MAP_FAILED) {
    munmap(file_, length_);
    file_ = static_cast<char*>(MAP_FAILED);
  }

  if (fd_ != -1) {
    close(fd_);
    fd_ = -1;
  }

  fileId_ = {};
}

ElfFile::OpenResult ElfFile::init() noexcept {
  if (length_ < 4) {
    return {kInvalidElfFile, "not an ELF file (too short)"};
  }

  std::array<char, 5> elfMagBuf = {{0, 0, 0, 0, 0}};
  if (::lseek(fd_, 0, SEEK_SET) != 0 || ::read(fd_, elfMagBuf.data(), 4) != 4) {
    return {kInvalidElfFile, "unable to read ELF file for magic number"};
  }
  if (std::strncmp(elfMagBuf.data(), ELFMAG, sizeof(ELFMAG)) != 0) {
    return {kInvalidElfFile, "invalid ELF magic"};
  }
  char c;
  if (::pread(fd_, &c, 1, length_ - 1) != 1) {
    auto msg =
        "The last bit of the mmaped memory is no longer valid. This may be "
        "caused by the original file being resized, "
        "deleted or otherwise modified.";
    return {kInvalidElfFile, msg};
  }

  if (::lseek(fd_, 0, SEEK_SET) != 0) {
    return {
        kInvalidElfFile,
        "unable to reset file descriptor after reading ELF magic number"};
  }

  auto& elfHeader = this->elfHeader();

#define EXPECTED_CLASS P1(ELFCLASS, FOLLY_ELF_NATIVE_CLASS)
#define P1(a, b) P2(a, b)
#define P2(a, b) a##b
  // Validate ELF class (32/64 bits)
  if (elfHeader.e_ident[EI_CLASS] != EXPECTED_CLASS) {
    return {kInvalidElfFile, "invalid ELF class"};
  }
#undef P1
#undef P2
#undef EXPECTED_CLASS

  // Validate ELF data encoding (LSB/MSB)
  static constexpr auto kExpectedEncoding =
      kIsLittleEndian ? ELFDATA2LSB : ELFDATA2MSB;
  if (elfHeader.e_ident[EI_DATA] != kExpectedEncoding) {
    return {kInvalidElfFile, "invalid ELF encoding"};
  }

  // Validate ELF version (1)
  if (elfHeader.e_ident[EI_VERSION] != EV_CURRENT ||
      elfHeader.e_version != EV_CURRENT) {
    return {kInvalidElfFile, "invalid ELF version"};
  }

  // We only support executable and shared object files
  if (elfHeader.e_type != ET_REL && elfHeader.e_type != ET_EXEC &&
      elfHeader.e_type != ET_DYN && elfHeader.e_type != ET_CORE) {
    return {kInvalidElfFile, "invalid ELF file type"};
  }

  // We support executable and shared object files and extracting debug info
  // from relocatable objects (.dwo sections in .o/.dwo files). The e_phnum and
  // e_phentsize header fileds are not required for relocatable files.
  // https://docs.oracle.com/cd/E19620-01/805-4693/6j4emccrq/index.html
  if (elfHeader.e_type != ET_REL) {
    if (elfHeader.e_phnum == 0) {
      return {kInvalidElfFile, "no program header!"};
    }

    if (elfHeader.e_phentsize != sizeof(ElfPhdr)) {
      return {kInvalidElfFile, "invalid program header entry size"};
    }
  }

  if (elfHeader.e_shentsize != sizeof(ElfShdr)) {
    if (elfHeader.e_shentsize != 0 || elfHeader.e_type != ET_CORE) {
      return {kInvalidElfFile, "invalid section header entry size"};
    }
  }

  // Program headers are sorted by load address, so the first PT_LOAD
  // header gives us the base address.
  if (elfHeader.e_type != ET_REL) {
    const ElfPhdr* programHeader =
        iterateProgramHeaders([](auto& h) { return h.p_type == PT_LOAD; });

    if (!programHeader) {
      return {kInvalidElfFile, "could not find base address"};
    }
    baseAddress_ = programHeader->p_vaddr;
  }

  return {kSuccess, nullptr};
}

const ElfShdr* ElfFile::getSectionByIndex(size_t idx) const noexcept {
  FOLLY_SAFE_CHECK(idx < elfHeader().e_shnum, "invalid section index");
  if (elfHeader().e_shoff + (idx + 1) * sizeof(ElfShdr) > length_) {
    // Handle ELFs with invalid internal offsets to program/section headers.
    return nullptr;
  }
  return &at<ElfShdr>(elfHeader().e_shoff + idx * sizeof(ElfShdr));
}

folly::StringPiece ElfFile::getSectionBody(
    const ElfShdr& section) const noexcept {
  return folly::StringPiece(file_ + section.sh_offset, section.sh_size);
}

void ElfFile::validateStringTable(const ElfShdr& stringTable) const noexcept {
  FOLLY_SAFE_CHECK(
      stringTable.sh_type == SHT_STRTAB, "invalid type for string table");

  const char* start = file_ + stringTable.sh_offset;
  // First and last bytes must be 0
  FOLLY_SAFE_CHECK(
      stringTable.sh_size == 0 ||
          (start[0] == '\0' && start[stringTable.sh_size - 1] == '\0'),
      "invalid string table");
}

const char* ElfFile::getString(
    const ElfShdr& stringTable, size_t offset) const noexcept {
  validateStringTable(stringTable);
  FOLLY_SAFE_CHECK(
      offset < stringTable.sh_size, "invalid offset in string table");

  return file_ + stringTable.sh_offset + offset;
}

const char* ElfFile::getSectionName(const ElfShdr& section) const noexcept {
  if (elfHeader().e_shstrndx == SHN_UNDEF) {
    return nullptr; // no section name string table
  }

  auto stringSection = getSectionByIndex(elfHeader().e_shstrndx);
  if (!stringSection) {
    return nullptr;
  }
  return getString(*stringSection, section.sh_name);
}

const ElfShdr* ElfFile::getSectionByName(const char* name) const noexcept {
  if (elfHeader().e_shstrndx == SHN_UNDEF) {
    return nullptr; // no section name string table
  }

  auto stringSection = getSectionByIndex(elfHeader().e_shstrndx);
  if (!stringSection) {
    return nullptr;
  }
  const ElfShdr& sectionNames = *stringSection;
  const char* start = file_ + sectionNames.sh_offset;

  // Find section with the appropriate sh_name offset
  const ElfShdr* foundSection = iterateSections([&](const ElfShdr& sh) {
    if (sh.sh_name >= sectionNames.sh_size) {
      return false;
    }
    return !strcmp(start + sh.sh_name, name);
  });
  return foundSection;
}

ElfFile::Symbol ElfFile::getDefinitionByAddress(
    uintptr_t address) const noexcept {
  Symbol foundSymbol{nullptr, nullptr};

  auto findSection = [&, address](const ElfShdr& section) {
    auto findSymbols = [&, address](const ElfSym& sym) {
      if (sym.st_shndx == SHN_UNDEF) {
        return false; // not a definition
      }
      if (address >= sym.st_value && address < sym.st_value + sym.st_size) {
        foundSymbol.first = &section;
        foundSymbol.second = &sym;
        return true;
      }

      return false;
    };

    return iterateSymbolsWithTypes(
        section, {STT_OBJECT, STT_FUNC, STT_GNU_IFUNC}, findSymbols);
  };

  // Try the .dynsym section first if it exists, it's smaller.
  (iterateSectionsWithType(SHT_DYNSYM, findSection) ||
   iterateSectionsWithType(SHT_SYMTAB, findSection));

  return foundSymbol;
}

ElfFile::Symbol ElfFile::getSymbolByName(
    const char* name, std::initializer_list<uint32_t> types) const noexcept {
  Symbol foundSymbol{nullptr, nullptr};

  auto findSection = [&](const ElfShdr& section) -> bool {
    // This section has no string table associated w/ its symbols; hence we
    // can't get names for them
    if (section.sh_link == SHN_UNDEF) {
      return false;
    }

    auto findSymbols = [&](const ElfSym& sym) -> bool {
      if (sym.st_shndx == SHN_UNDEF) {
        return false; // not a definition
      }
      if (sym.st_name == 0) {
        return false; // no name for this symbol
      }
      auto linkSection = getSectionByIndex(section.sh_link);
      if (!linkSection) {
        return false;
      }
      const char* sym_name = getString(*linkSection, sym.st_name);
      if (strcmp(sym_name, name) == 0) {
        foundSymbol.first = &section;
        foundSymbol.second = &sym;
        return true;
      }

      return false;
    };

    return iterateSymbolsWithTypes(section, types, findSymbols);
  };

  // Try the .dynsym section first if it exists, it's smaller.
  iterateSectionsWithType(SHT_DYNSYM, findSection) ||
      iterateSectionsWithType(SHT_SYMTAB, findSection);

  return foundSymbol;
}

const ElfShdr* ElfFile::getSectionContainingAddress(
    ElfAddr addr) const noexcept {
  return iterateSections([&](const ElfShdr& sh) -> bool {
    return (addr >= sh.sh_addr) && (addr < (sh.sh_addr + sh.sh_size));
  });
}

const char* ElfFile::getSymbolName(const Symbol& symbol) const noexcept {
  if (!symbol.first || !symbol.second) {
    return nullptr;
  }

  if (symbol.second->st_name == 0) {
    return nullptr; // symbol has no name
  }

  if (symbol.first->sh_link == SHN_UNDEF) {
    return nullptr; // symbol table has no strings
  }

  auto linkSection = getSectionByIndex(symbol.first->sh_link);
  if (!linkSection) {
    return nullptr;
  }
  return getString(*linkSection, symbol.second->st_name);
}

std::pair<const int, char const*> ElfFile::posixFadvise(
    off_t offset, off_t len, int const advice) const noexcept {
  if (fd_ == -1) {
    return {1, "file not open"};
  }
  int res = posix_fadvise(fd_, offset, len, advice);
  if (res != 0) {
    return {res, "posix_fadvise failed for file"};
  }
  return {res, ""};
}

std::pair<const int, char const*> ElfFile::posixFadvise(
    int const advice) const noexcept {
  return posixFadvise(0, 0, advice);
}

} // namespace symbolizer
} // namespace folly

#endif // FOLLY_HAVE_ELF
