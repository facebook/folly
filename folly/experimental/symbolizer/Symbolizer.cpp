/*
 * Copyright 2014 Facebook, Inc.
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

#include <folly/experimental/symbolizer/Symbolizer.h>

#include <limits.h>
#include <cstdio>
#include <iostream>
#include <map>

#ifdef __GNUC__
#include <ext/stdio_filebuf.h>
#include <ext/stdio_sync_filebuf.h>
#endif

#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>

#include <folly/experimental/symbolizer/Elf.h>
#include <folly/experimental/symbolizer/Dwarf.h>
#include <folly/experimental/symbolizer/LineReader.h>


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

ElfCache* defaultElfCache() {
  static constexpr size_t defaultCapacity = 500;
  static ElfCache cache(defaultCapacity);
  return &cache;
}

}  // namespace

void SymbolizedFrame::set(const std::shared_ptr<ElfFile>& file,
                          uintptr_t address) {
  clear();
  found = true;

  address += file->getBaseAddress();
  auto sym = file->getDefinitionByAddress(address);
  if (!sym.first) {
    return;
  }

  file_ = file;
  name = file->getSymbolName(sym);

  Dwarf(file.get()).findAddress(address, location);
}


Symbolizer::Symbolizer(ElfCacheBase* cache)
  : cache_(cache ?: defaultElfCache()) {
}

void Symbolizer::symbolize(const uintptr_t* addresses,
                           SymbolizedFrame* frames,
                           size_t addressCount) {
  size_t remaining = 0;
  for (size_t i = 0; i < addressCount; ++i) {
    auto& frame = frames[i];
    if (!frame.found) {
      ++remaining;
      frame.clear();
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
    std::shared_ptr<ElfFile> elfFile;

    // See if any addresses are here
    for (size_t i = 0; i < addressCount; ++i) {
      auto& frame = frames[i];
      if (frame.found) {
        continue;
      }

      uintptr_t address = addresses[i];

      if (from > address || address >= to) {
        continue;
      }

      // Found
      frame.found = true;
      --remaining;

      // Open the file on first use
      if (first) {
        first = false;
        elfFile = cache_->getFile(fileName);
      }

      if (!elfFile) {
        continue;
      }

      // Undo relocation
      frame.set(elfFile, address - from);
    }
  }

  closeNoInt(fd);
}

namespace {
const char kHexChars[] = "0123456789abcdef";
const SymbolizePrinter::Color kAddressColor = SymbolizePrinter::Color::BLUE;
const SymbolizePrinter::Color kFunctionColor = SymbolizePrinter::Color::PURPLE;
const SymbolizePrinter::Color kFileColor = SymbolizePrinter::Color::DEFAULT;
}  // namespace

constexpr char AddressFormatter::bufTemplate[];

AddressFormatter::AddressFormatter() {
  memcpy(buf_, bufTemplate, sizeof(buf_));
}

folly::StringPiece AddressFormatter::format(uintptr_t address) {
  // Can't use sprintf, not async-signal-safe
  static_assert(sizeof(uintptr_t) <= 8, "huge uintptr_t?");
  char* end = buf_ + sizeof(buf_) - 1 - (16 - 2 * sizeof(uintptr_t));
  char* p = end;
  *p-- = '\0';
  while (address != 0) {
    *p-- = kHexChars[address & 0xf];
    address >>= 4;
  }

  return folly::StringPiece(buf_, end);
}

void SymbolizePrinter::print(uintptr_t address, const SymbolizedFrame& frame) {
  if (options_ & TERSE) {
    printTerse(address, frame);
    return;
  }

  SCOPE_EXIT { color(Color::DEFAULT); };

  color(kAddressColor);

  AddressFormatter formatter;
  doPrint(formatter.format(address));

  const char padBuf[] = "                       ";
  folly::StringPiece pad(padBuf,
                         sizeof(padBuf) - 1 - (16 - 2 * sizeof(uintptr_t)));

  color(kFunctionColor);
  if (!frame.found) {
    doPrint(" (not found)");
    return;
  }

  if (!frame.name || frame.name[0] == '\0') {
    doPrint(" (unknown)");
  } else {
    char demangledBuf[1024];
    demangle(frame.name, demangledBuf, sizeof(demangledBuf));
    doPrint(" ");
    doPrint(demangledBuf[0] == '\0' ? frame.name : demangledBuf);
  }

  if (!(options_ & NO_FILE_AND_LINE)) {
    color(kFileColor);
    char fileBuf[PATH_MAX];
    fileBuf[0] = '\0';
    if (frame.location.hasFileAndLine) {
      frame.location.file.toBuffer(fileBuf, sizeof(fileBuf));
      doPrint("\n");
      doPrint(pad);
      doPrint(fileBuf);

      char buf[22];
      uint32_t n = uint64ToBufferUnsafe(frame.location.line, buf);
      doPrint(":");
      doPrint(StringPiece(buf, n));
    }

    if (frame.location.hasMainFile) {
      char mainFileBuf[PATH_MAX];
      mainFileBuf[0] = '\0';
      frame.location.mainFile.toBuffer(mainFileBuf, sizeof(mainFileBuf));
      if (!frame.location.hasFileAndLine || strcmp(fileBuf, mainFileBuf)) {
        doPrint("\n");
        doPrint(pad);
        doPrint("-> ");
        doPrint(mainFileBuf);
      }
    }
  }
}

namespace {

const std::map<SymbolizePrinter::Color, std::string> kColorMap = {
  { SymbolizePrinter::Color::DEFAULT,  "\x1B[0m" },
  { SymbolizePrinter::Color::RED,  "\x1B[31m" },
  { SymbolizePrinter::Color::GREEN,  "\x1B[32m" },
  { SymbolizePrinter::Color::YELLOW,  "\x1B[33m" },
  { SymbolizePrinter::Color::BLUE,  "\x1B[34m" },
  { SymbolizePrinter::Color::CYAN,  "\x1B[36m" },
  { SymbolizePrinter::Color::WHITE,  "\x1B[37m" },
  { SymbolizePrinter::Color::PURPLE,  "\x1B[35m" },
};

}

void SymbolizePrinter::color(SymbolizePrinter::Color color) {
  if ((options_ & COLOR) == 0 &&
      ((options_ & COLOR_IF_TTY) == 0 || !isTty_)) {
    return;
  }
  auto it = kColorMap.find(color);
  if (it == kColorMap.end()) {
    return;
  }
  doPrint(it->second);
}

void SymbolizePrinter::println(uintptr_t address,
                               const SymbolizedFrame& frame) {
  print(address, frame);
  doPrint("\n");
}

void SymbolizePrinter::printTerse(uintptr_t address,
                                  const SymbolizedFrame& frame) {
  if (frame.found && frame.name && frame.name[0] != '\0') {
    char demangledBuf[1024] = {0};
    demangle(frame.name, demangledBuf, sizeof(demangledBuf));
    doPrint(demangledBuf[0] == '\0' ? frame.name : demangledBuf);
  } else {
    // Can't use sprintf, not async-signal-safe
    static_assert(sizeof(uintptr_t) <= 8, "huge uintptr_t?");
    char buf[] = "0x0000000000000000";
    char* end = buf + sizeof(buf) - 1 - (16 - 2 * sizeof(uintptr_t));
    char* p = end;
    *p-- = '\0';
    while (address != 0) {
      *p-- = kHexChars[address & 0xf];
      address >>= 4;
    }
    doPrint(StringPiece(buf, end));
  }
}

void SymbolizePrinter::println(const uintptr_t* addresses,
                               const SymbolizedFrame* frames,
                               size_t frameCount) {
  for (size_t i = 0; i < frameCount; ++i) {
    println(addresses[i], frames[i]);
  }
}

namespace {

int getFD(const std::ios& stream) {
#ifdef __GNUC__
  std::streambuf* buf = stream.rdbuf();
  using namespace __gnu_cxx;

  {
    auto sbuf = dynamic_cast<stdio_sync_filebuf<char>*>(buf);
    if (sbuf) {
      return fileno(sbuf->file());
    }
  }
  {
    auto sbuf = dynamic_cast<stdio_filebuf<char>*>(buf);
    if (sbuf) {
      return sbuf->fd();
    }
  }
#endif  // __GNUC__
  return -1;
}

bool isTty(int options, int fd) {
  return ((options & SymbolizePrinter::TERSE) == 0 &&
          (options & SymbolizePrinter::COLOR_IF_TTY) != 0 &&
          fd >= 0 && ::isatty(fd));
}

}  // anonymous namespace

OStreamSymbolizePrinter::OStreamSymbolizePrinter(std::ostream& out, int options)
  : SymbolizePrinter(options, isTty(options, getFD(out))),
    out_(out) {
}

void OStreamSymbolizePrinter::doPrint(StringPiece sp) {
  out_ << sp;
}

FDSymbolizePrinter::FDSymbolizePrinter(int fd, int options, size_t bufferSize)
  : SymbolizePrinter(options, isTty(options, fd)),
    fd_(fd),
    buffer_(bufferSize ? IOBuf::create(bufferSize) : nullptr) {
}

FDSymbolizePrinter::~FDSymbolizePrinter() {
  flush();
}

void FDSymbolizePrinter::doPrint(StringPiece sp) {
  if (buffer_) {
    if (sp.size() > buffer_->tailroom()) {
      flush();
      writeFull(fd_, sp.data(), sp.size());
    } else {
      memcpy(buffer_->writableTail(), sp.data(), sp.size());
      buffer_->append(sp.size());
    }
  } else {
    writeFull(fd_, sp.data(), sp.size());
  }
}

void FDSymbolizePrinter::flush() {
  if (buffer_ && !buffer_->empty()) {
    writeFull(fd_, buffer_->data(), buffer_->length());
    buffer_->clear();
  }
}

FILESymbolizePrinter::FILESymbolizePrinter(FILE* file, int options)
  : SymbolizePrinter(options, isTty(options, fileno(file))),
    file_(file) {
}

void FILESymbolizePrinter::doPrint(StringPiece sp) {
  fwrite(sp.data(), 1, sp.size(), file_);
}

void StringSymbolizePrinter::doPrint(StringPiece sp) {
  buf_.append(sp.data(), sp.size());
}

}  // namespace symbolizer
}  // namespace folly
