/*
 * Copyright 2012 Facebook, Inc.
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


#include "folly/experimental/symbolizer/Symbolizer.h"

#include <boost/regex.hpp>
#include <glog/logging.h>

#include "folly/experimental/symbolizer/Elf.h"
#include "folly/experimental/symbolizer/Dwarf.h"
#include "folly/Range.h"
#include "folly/FBString.h"
#include "folly/String.h"
#include "folly/experimental/Gen.h"
#include "folly/experimental/FileGen.h"
#include "folly/experimental/StringGen.h"

namespace folly {
namespace symbolizer {

namespace {
StringPiece sp(const boost::csub_match& m) {
  return StringPiece(m.first, m.second);
}

uint64_t fromHex(StringPiece s) {
  // Make a copy; we need a null-terminated string for strtoull
  fbstring str(s.data(), s.size());
  const char* p = str.c_str();
  char* end;
  uint64_t val = strtoull(p, &end, 16);
  CHECK(*p != '\0' && *end == '\0');
  return val;
}

struct MappedFile {
  uintptr_t begin;
  uintptr_t end;
  std::string name;
};

}  // namespace

bool Symbolizer::symbolize(uintptr_t address, StringPiece& symbolName,
                           Dwarf::LocationInfo& location) {
  symbolName.clear();
  location = Dwarf::LocationInfo();

  // Entry in /proc/self/maps
  static const boost::regex mapLineRegex(
    "([[:xdigit:]]+)-([[:xdigit:]]+)"  // from-to
    "\\s+"
    "[\\w-]+"  // permissions
    "\\s+"
    "([[:xdigit:]]+)"  // offset
    "\\s+"
    "[[:xdigit:]]+:[[:xdigit:]]+"  // device, minor:major
    "\\s+"
    "\\d+"  // inode
    "\\s*"
    "(.*)");  // file name

  boost::cmatch match;

  MappedFile foundFile;
  bool error = gen::byLine("/proc/self/maps") |
    [&] (StringPiece line) -> bool {
      CHECK(boost::regex_match(line.begin(), line.end(), match, mapLineRegex));
      uint64_t begin = fromHex(sp(match[1]));
      uint64_t end = fromHex(sp(match[2]));
      uint64_t fileOffset = fromHex(sp(match[3]));
      if (fileOffset != 0) {
        return true;  // main mapping starts at 0
      }

      if (begin <= address && address < end) {
        foundFile.begin = begin;
        foundFile.end = end;
        foundFile.name.assign(match[4].first, match[4].second);
        return false;
      }

      return true;
    };

  if (error) {
    return false;
  }

  auto& elfFile = getFile(foundFile.name);
  // Undo relocation
  uintptr_t origAddress = address - foundFile.begin + elfFile.getBaseAddress();

  auto sym = elfFile.getDefinitionByAddress(origAddress);
  if (!sym.first) {
    return false;
  }

  auto name = elfFile.getSymbolName(sym);
  if (name) {
    symbolName = name;
  }

  Dwarf(&elfFile).findAddress(origAddress, location);
  return true;
}

ElfFile& Symbolizer::getFile(const std::string& name) {
  auto pos = elfFiles_.find(name);
  if (pos != elfFiles_.end()) {
    return pos->second;
  }

  return elfFiles_.insert(
      std::make_pair(name, ElfFile(name.c_str()))).first->second;
}

void Symbolizer::write(std::ostream& out, uintptr_t address,
                       StringPiece symbolName,
                       const Dwarf::LocationInfo& location) {
  char buf[20];
  sprintf(buf, "%#18jx", address);
  out << "    @ " << buf;

  if (!symbolName.empty()) {
    out << " " << demangle(symbolName.toString().c_str());

    std::string file;
    if (location.hasFileAndLine) {
      file = location.file.toString();
      out << "   " << file << ":" << location.line;
    }

    std::string mainFile;
    if (location.hasMainFile) {
      mainFile = location.mainFile.toString();
      if (!location.hasFileAndLine || file != mainFile) {
        out << "\n                         (compiling "
            << location.mainFile << ")";
      }
    }
  } else {
    out << " (unknown)";
  }
  out << "\n";
}

}  // namespace symbolizer
}  // namespace folly
