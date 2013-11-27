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

// Must be first to ensure that UNW_LOCAL_ONLY is defined
#define UNW_LOCAL_ONLY 1
#include <libunwind.h>

#include "folly/experimental/symbolizer/Symbolizer.h"

#include <limits.h>

#include "folly/Conv.h"
#include "folly/FileUtil.h"
#include "folly/String.h"

#include "folly/experimental/symbolizer/Elf.h"
#include "folly/experimental/symbolizer/Dwarf.h"
#include "folly/experimental/symbolizer/LineReader.h"

namespace folly {
namespace symbolizer {

namespace {

/**
 * Read a hex value.
 */
uintptr_t readHex(StringPiece& sp) {
  uintptr_t val = 0;
  const char* p = sp.begin();
  for (; p != sp.end(); ++p) {
    unsigned int v;
    if (*p >= '0' && *p <= '9') {
      v = (*p - '0');
    } else if (*p >= 'a' && *p <= 'f') {
      v = (*p - 'a') + 10;
    } else if (*p >= 'A' && *p <= 'F') {
      v = (*p - 'A') + 10;
    } else {
      break;
    }
    val = (val << 4) + v;
  }
  sp.assign(p, sp.end());
  return val;
}

/**
 * Skip over non-space characters.
 */
void skipNS(StringPiece& sp) {
  const char* p = sp.begin();
  for (; p != sp.end() && (*p != ' ' && *p != '\t'); ++p) { }
  sp.assign(p, sp.end());
}

/**
 * Skip over space and tab characters.
 */
void skipWS(StringPiece& sp) {
  const char* p = sp.begin();
  for (; p != sp.end() && (*p == ' ' || *p == '\t'); ++p) { }
  sp.assign(p, sp.end());
}

/**
 * Parse a line from /proc/self/maps
 */
bool parseProcMapsLine(StringPiece line,
                       uintptr_t& from, uintptr_t& to,
                       StringPiece& fileName) {
  // from     to       perm offset   dev   inode             path
  // 00400000-00405000 r-xp 00000000 08:03 35291182          /bin/cat
  if (line.empty()) {
    return false;
  }

  // Remove trailing newline, if any
  if (line.back() == '\n') {
    line.pop_back();
  }

  // from
  from = readHex(line);
  if (line.empty() || line.front() != '-') {
    return false;
  }
  line.pop_front();

  // to
  to = readHex(line);
  if (line.empty() || line.front() != ' ') {
    return false;
  }
  line.pop_front();

  // perms
  skipNS(line);
  if (line.empty() || line.front() != ' ') {
    return false;
  }
  line.pop_front();

  uintptr_t fileOffset = readHex(line);
  if (line.empty() || line.front() != ' ') {
    return false;
  }
  line.pop_front();
  if (fileOffset != 0) {
    return false;  // main mapping starts at 0
  }

  // dev
  skipNS(line);
  if (line.empty() || line.front() != ' ') {
    return false;
  }
  line.pop_front();

  // inode
  skipNS(line);
  if (line.empty() || line.front() != ' ') {
    return false;
  }

  skipWS(line);
  if (line.empty()) {
    fileName.clear();
    return true;
  }

  fileName = line;
  return true;
}

}  // namespace

ssize_t getStackTrace(AddressInfo* addresses,
                      size_t maxAddresses,
                      size_t skip) {
  unw_context_t uctx;
  int r = unw_getcontext(&uctx);
  if (r < 0) {
    return -1;
  }

  unw_cursor_t cursor;
  size_t idx = 0;
  bool first = true;
  while (idx < maxAddresses) {
    if (first) {
      first = false;
      r = unw_init_local(&cursor, &uctx);
    } else {
      r = unw_step(&cursor);
      if (r == 0) {
        break;
      }
    }
    if (r < 0) {
      return -1;
    }

    if (skip != 0) {
      --skip;
      continue;
    }
    unw_word_t ip;
    int rr = unw_get_reg(&cursor, UNW_REG_IP, &ip);
    if (rr < 0) {
      return -1;
    }

    // If error, assume not a signal frame
    rr = unw_is_signal_frame(&cursor);

    addresses[idx++] = AddressInfo(ip, (rr > 0));
  }

  if (r < 0) {
    return -1;
  }

  return idx;
}

void Symbolizer::symbolize(AddressInfo* addresses, size_t addressCount) {
  size_t remaining = 0;
  for (size_t i = 0; i < addressCount; ++i) {
    auto& ainfo = addresses[i];
    if (!ainfo.found) {
      ++remaining;
      ainfo.name.clear();
      ainfo.location = Dwarf::LocationInfo();
    }
  }

  if (remaining == 0) {  // we're done
    return;
  }

  int fd = openNoInt("/proc/self/maps", O_RDONLY);
  if (fd == -1) {
    return;
  }

  char buf[PATH_MAX + 100];  // Long enough for any line
  LineReader reader(fd, buf, sizeof(buf));

  char fileNameBuf[PATH_MAX];

  while (remaining != 0) {
    StringPiece line;
    if (reader.readLine(line) != LineReader::kReading) {
      break;
    }

    // Parse line
    uintptr_t from;
    uintptr_t to;
    StringPiece fileName;
    if (!parseProcMapsLine(line, from, to, fileName)) {
      continue;
    }

    bool first = true;
    ElfFile* elfFile = nullptr;

    // See if any addresses are here
    for (size_t i = 0; i < addressCount; ++i) {
      auto& ainfo = addresses[i];
      if (ainfo.found) {
        continue;
      }

      uintptr_t address = ainfo.address;

      // If the next address (closer to the top of the stack) was a signal
      // frame, then this is the *resume* address, which is the address
      // after the location where the signal was caught. This might be in
      // the next function, so subtract 1 before symbolizing.
      if (i != 0 && addresses[i-1].isSignalFrame) {
        --address;
      }

      if (from > address || address >= to) {
        continue;
      }

      // Found
      ainfo.found = true;
      --remaining;

      // Open the file on first use
      if (first) {
        first = false;
        if (fileCount_ < kMaxFiles &&
            !fileName.empty() &&
            fileName.size() < sizeof(fileNameBuf)) {
          memcpy(fileNameBuf, fileName.data(), fileName.size());
          fileNameBuf[fileName.size()] = '\0';
          auto& f = files_[fileCount_++];
          if (f.openNoThrow(fileNameBuf) != -1) {
            elfFile = &f;
          }
        }
      }

      if (!elfFile) {
        continue;
      }

      // Undo relocation
      uintptr_t fileAddress = address - from + elfFile->getBaseAddress();
      auto sym = elfFile->getDefinitionByAddress(fileAddress);
      if (!sym.first) {
        continue;
      }
      auto name = elfFile->getSymbolName(sym);
      if (name) {
        ainfo.name = name;
      }

      Dwarf(elfFile).findAddress(fileAddress, ainfo.location);
    }
  }

  closeNoInt(fd);
}

namespace {
const char kHexChars[] = "0123456789abcdef";
}  // namespace

void SymbolizePrinter::print(const AddressInfo& ainfo) {
  uintptr_t address = ainfo.address;
  // Can't use sprintf, not async-signal-safe
  static_assert(sizeof(uintptr_t) <= 8, "huge uintptr_t?");
  char buf[] = "    @ 0000000000000000";
  char* end = buf + sizeof(buf) - 1 - (16 - 2 * sizeof(uintptr_t));
  const char padBuf[] = "                       ";
  folly::StringPiece pad(padBuf,
                         sizeof(padBuf) - 1 - (16 - 2 * sizeof(uintptr_t)));
  char* p = end;
  *p-- = '\0';
  while (address != 0) {
    *p-- = kHexChars[address & 0xf];
    address >>= 4;
  }
  doPrint(folly::StringPiece(buf, end));

  char mangledBuf[1024];
  if (!ainfo.found) {
    doPrint(" (not found)\n");
    return;
  }

  if (ainfo.name.empty()) {
    doPrint(" (unknown)\n");
  } else if (ainfo.name.size() >= sizeof(mangledBuf)) {
    doPrint(" ");
    doPrint(ainfo.name);
    doPrint("\n");
  } else {
    memcpy(mangledBuf, ainfo.name.data(), ainfo.name.size());
    mangledBuf[ainfo.name.size()] = '\0';

    char demangledBuf[1024];
    demangle(mangledBuf, demangledBuf, sizeof(demangledBuf));
    doPrint(" ");
    doPrint(demangledBuf);
    doPrint("\n");
  }

  char fileBuf[PATH_MAX];
  fileBuf[0] = '\0';
  if (ainfo.location.hasFileAndLine) {
    ainfo.location.file.toBuffer(fileBuf, sizeof(fileBuf));
    doPrint(pad);
    doPrint(fileBuf);

    char buf[22];
    uint32_t n = uint64ToBufferUnsafe(ainfo.location.line, buf);
    doPrint(":");
    doPrint(StringPiece(buf, n));
    doPrint("\n");
  }

  if (ainfo.location.hasMainFile) {
    char mainFileBuf[PATH_MAX];
    mainFileBuf[0] = '\0';
    ainfo.location.mainFile.toBuffer(mainFileBuf, sizeof(mainFileBuf));
    if (!ainfo.location.hasFileAndLine || strcmp(fileBuf, mainFileBuf)) {
      doPrint(pad);
      doPrint("-> ");
      doPrint(mainFileBuf);
      doPrint("\n");
    }
  }
}

void SymbolizePrinter::print(const AddressInfo* addresses,
                             size_t addressCount) {
  for (size_t i = 0; i < addressCount; ++i) {
    auto& ainfo = addresses[i];
    print(ainfo);
  }
}

void OStreamSymbolizePrinter::doPrint(StringPiece sp) {
  out_ << sp;
}

void FDSymbolizePrinter::doPrint(StringPiece sp) {
  writeFull(fd_, sp.data(), sp.size());
}

std::ostream& operator<<(std::ostream& out, const AddressInfo& ainfo) {
  OStreamSymbolizePrinter osp(out);
  osp.print(ainfo);
  return out;
}

}  // namespace symbolizer
}  // namespace folly
