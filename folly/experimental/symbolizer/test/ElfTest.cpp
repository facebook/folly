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

#include <folly/FileUtil.h>
#include <folly/experimental/TestUtil.h>
#include <folly/experimental/symbolizer/detail/Debug.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAVE_ELF

using folly::symbolizer::ElfFile;

// Add some symbols for testing. Note that we have to be careful with type
// signatures here to prevent name mangling
uint64_t kIntegerValue = 1234567890ULL;
const char* kStringValue = "coconuts";
extern "C" {
int sum_func(int lhs, int rhs) {
  return lhs + rhs;
}
int sub_func(int lhs, int rhs) {
  return lhs - rhs;
}
}

const char* const kDefaultElf = "/proc/self/exe";
class ElfTest : public ::testing::Test {
 protected:
  ElfFile elfFile_{kDefaultElf};
};

TEST_F(ElfTest, IntegerValue) {
  auto sym = elfFile_.getSymbolByName("kIntegerValue");
  EXPECT_NE(nullptr, sym.first) << "Failed to look up symbol kIntegerValue";
  EXPECT_EQ(kIntegerValue, *elfFile_.getSymbolValue<uint64_t>(sym.second));
}

TEST_F(ElfTest, PointerValue) {
  auto sym = elfFile_.getSymbolByName("kStringValue");
  EXPECT_NE(nullptr, sym.first) << "Failed to look up symbol kStringValue";
  ElfW(Addr) addr = *elfFile_.getSymbolValue<ElfW(Addr)>(sym.second);
  // Let's check the address for the symbol against our own copy of
  // kStringValue.
  // For PIE binaries we need to adjust the address due to relocation.
  auto binaryOffset = folly::symbolizer::detail::get_r_debug()->r_map->l_addr;
  EXPECT_EQ(
      static_cast<const void*>(&kStringValue),
      reinterpret_cast<const void*>(binaryOffset + sym.second->st_value));
  if (binaryOffset == 0) { // non-PIE
    // Only do this check if we have a non-PIE. For the PIE case, the compiler
    // could put a 0 in the .data section for kStringValue, and then rely on
    // the dynamic linker to fill in the actual pointer to the .rodata section
    // via relocation; the actual offset of the string is encoded in the
    // .rela.dyn section, which isn't parsed in the current implementation of
    // ElfFile.
    const char* str = elfFile_.getAddressValue<const char>(addr);
    EXPECT_STREQ(kStringValue, str);
  }
}

TEST_F(ElfTest, SymbolByName) {
  auto sym = elfFile_.getSymbolByName("sum_func");
  EXPECT_NE(nullptr, sym.first) << "Failed to look up symbol sum_func";

  sym = elfFile_.getSymbolByName("sum_func", {STT_OBJECT});
  EXPECT_EQ(nullptr, sym.first) << "Unexpectedly look up of non-object symbol.";

  sym = elfFile_.getSymbolByName("sum_func", {STT_FUNC});
  EXPECT_NE(nullptr, sym.first)
      << "Failed to look up symbol sum_func when specifying type";

  sym = elfFile_.getSymbolByName("kIntegerValue", {STT_FUNC});
  EXPECT_EQ(nullptr, sym.first) << "Unexpected look up of non-func symbol.";

  sym = elfFile_.getSymbolByName("kIntegerValue", {STT_OBJECT});
  EXPECT_NE(nullptr, sym.first)
      << "Failed to look up symbol kIntegerValue when specifying type";
}

TEST_F(ElfTest, SymbolsByNameSuccess) {
  std::vector<std::string> names = {"sum_func", "sub_func"};
  auto result = elfFile_.getSymbolsByName(names, {STT_FUNC});

  EXPECT_EQ(names.size(), result.size());
  EXPECT_TRUE(result.find("sum_func") != result.end());
  EXPECT_TRUE(result.find("sub_func") != result.end());

  const auto& sumFuncSymbol = result.at("sum_func");
  EXPECT_NE(nullptr, sumFuncSymbol.first);
  EXPECT_NE(nullptr, sumFuncSymbol.second);

  const auto& subFuncSymbol = result.at("sub_func");
  EXPECT_NE(nullptr, subFuncSymbol.first);
  EXPECT_NE(nullptr, subFuncSymbol.second);
}

TEST_F(ElfTest, SymbolsByNamePartial) {
  std::vector<std::string> names = {"sum_func", "sub_func", "foo_func"};
  auto result = elfFile_.getSymbolsByName(names, {STT_FUNC});

  EXPECT_EQ(names.size(), result.size());
  EXPECT_TRUE(result.find("sum_func") != result.end());
  EXPECT_TRUE(result.find("sub_func") != result.end());
  EXPECT_TRUE(result.find("foo_func") != result.end());

  const auto& fooFuncSymbol = result.at("foo_func");
  EXPECT_EQ(nullptr, fooFuncSymbol.first);
  EXPECT_EQ(nullptr, fooFuncSymbol.second);
}

TEST_F(ElfTest, SymbolsByNameEmpty) {
  std::vector<std::string> names;
  auto result = elfFile_.getSymbolsByName(names, {STT_FUNC});

  EXPECT_EQ(0, result.size());
  EXPECT_EQ(names.size(), result.size());
}

TEST_F(ElfTest, iterateProgramHeaders) {
  auto phdr = elfFile_.iterateProgramHeaders(
      [](auto& h) { return h.p_type == PT_LOAD; });
  EXPECT_NE(nullptr, phdr);
  EXPECT_GE(phdr->p_filesz, 0);
}

TEST_F(ElfTest, TinyNonElfFile) {
  folly::test::TemporaryFile tmpFile;
  const static folly::StringPiece contents = "!";
  folly::writeFull(tmpFile.fd(), contents.data(), contents.size());

  ElfFile elfFile;
  auto res = elfFile.openNoThrow(tmpFile.path().c_str());
  EXPECT_EQ(ElfFile::kInvalidElfFile, res.code);
  EXPECT_STREQ("not an ELF file (too short)", res.msg);
}

TEST_F(ElfTest, NonElfScript) {
  folly::test::TemporaryFile tmpFile;
  const static folly::StringPiece contents =
      "#!/bin/sh\necho I'm small non-ELF executable\n";
  folly::writeFull(tmpFile.fd(), contents.data(), contents.size());

  ElfFile elfFile;
  auto res = elfFile.openNoThrow(tmpFile.path().c_str());
  EXPECT_EQ(ElfFile::kInvalidElfFile, res.code);
  EXPECT_STREQ("invalid ELF magic", res.msg);
}

TEST_F(ElfTest, FailToOpenLargeFilename) {
  // ElfFile used to segfault if it failed to open large filenames
  folly::test::TemporaryDirectory tmpDir;
  auto elfFile = std::make_unique<ElfFile>(); // on the heap so asan can see it
  auto const largeNonExistingName = (tmpDir.path() / std::string(1000, 'x'));
  ASSERT_EQ(
      ElfFile::kSystemError,
      elfFile->openNoThrow(largeNonExistingName.c_str()));
  ASSERT_EQ(
      ElfFile::kSystemError,
      elfFile->openNoThrow(largeNonExistingName.c_str()));
  EXPECT_EQ(ElfFile::kSuccess, elfFile->openNoThrow(kDefaultElf));
}

TEST_F(ElfTest, PosixFadvise) {
  auto res = elfFile_.posixFadvise(POSIX_FADV_DONTNEED);
  EXPECT_EQ(0, res.first);
  EXPECT_STREQ("", res.second);
}

TEST_F(ElfTest, PosixFadviseOffSetAndLen) {
  auto posixFadviseCalled = false;
  elfFile_.iterateSections(
      [&](const folly::symbolizer::ElfShdr& section) -> bool {
        if (section.sh_type == SHT_SYMTAB) {
          posixFadviseCalled = true;
          auto res = elfFile_.posixFadvise(
              section.sh_offset, section.sh_size, POSIX_FADV_DONTNEED);
          EXPECT_EQ(0, res.first);
          EXPECT_STREQ("", res.second);
        }
        return false;
      });
  EXPECT_TRUE(posixFadviseCalled);
}

TEST_F(ElfTest, PosixFadviseNotOpen) {
  folly::test::TemporaryFile tmpFile;
  const static folly::StringPiece contents = "!";
  folly::writeFull(tmpFile.fd(), contents.data(), contents.size());

  ElfFile elfFile;
  elfFile.openNoThrow(tmpFile.path().c_str());
  auto res = elfFile.posixFadvise(POSIX_FADV_DONTNEED);
  EXPECT_EQ(1, res.first);
  EXPECT_STREQ("file not open", res.second);
}

TEST_F(ElfTest, PosixFadviseBadAdvice) {
  auto res = elfFile_.posixFadvise(10000);
  EXPECT_NE(0, res.first);
  EXPECT_STREQ("posix_fadvise failed for file", res.second);
}

#endif
