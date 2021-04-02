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

#include <folly/experimental/symbolizer/SymbolizePrinter.h>

#include <folly/Demangle.h>
#include <folly/FileUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/io/IOBuf.h>
#include <folly/lang/ToAscii.h>

#ifdef __GLIBCXX__
#include <ext/stdio_filebuf.h>
#include <ext/stdio_sync_filebuf.h>
#endif

namespace folly {
namespace symbolizer {

namespace {
constexpr char kHexChars[] = "0123456789abcdef";
constexpr auto kAddressColor = SymbolizePrinter::Color::BLUE;
constexpr auto kFunctionColor = SymbolizePrinter::Color::PURPLE;
constexpr auto kFileColor = SymbolizePrinter::Color::DEFAULT;

#ifdef _WIN32
constexpr size_t kPathMax = 4096;
#else
constexpr size_t kPathMax = PATH_MAX;
#endif
} // namespace

constexpr char AddressFormatter::bufTemplate[];
constexpr std::array<const char*, SymbolizePrinter::Color::NUM>
    SymbolizePrinter::kColorMap;

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

void SymbolizePrinter::print(const SymbolizedFrame& frame) {
  if (options_ & TERSE) {
    printTerse(frame);
    return;
  }

  SCOPE_EXIT { color(Color::DEFAULT); };

  if (!(options_ & NO_FRAME_ADDRESS) && !(options_ & TERSE_FILE_AND_LINE)) {
    color(kAddressColor);

    AddressFormatter formatter;
    doPrint(formatter.format(frame.addr));
  }

  const char padBuf[] = "                       ";
  folly::StringPiece pad(
      padBuf, sizeof(padBuf) - 1 - (16 - 2 * sizeof(uintptr_t)));

  color(kFunctionColor);
  if (!frame.found) {
    doPrint(" (not found)");
    return;
  }

  if (!(options_ & TERSE_FILE_AND_LINE)) {
    if (!frame.name || frame.name[0] == '\0') {
      doPrint(" (unknown)");
    } else {
      char demangledBuf[2048];
      folly::demangle(frame.name, demangledBuf, sizeof(demangledBuf));
      doPrint(" ");
      doPrint(demangledBuf[0] == '\0' ? frame.name : demangledBuf);
    }
  }

  if (!(options_ & NO_FILE_AND_LINE)) {
    color(kFileColor);
    char fileBuf[kPathMax];
    fileBuf[0] = '\0';
    if (frame.location.hasFileAndLine) {
      frame.location.file.toBuffer(fileBuf, sizeof(fileBuf));
      if (!(options_ & TERSE_FILE_AND_LINE)) {
        doPrint("\n");
        doPrint(pad);
      }
      doPrint(fileBuf);

      char buf[to_ascii_size_max_decimal<decltype(frame.location.line)>];
      uint32_t n = to_ascii_decimal(buf, frame.location.line);
      doPrint(":");
      doPrint(StringPiece(buf, n));
    } else {
      if ((options_ & TERSE_FILE_AND_LINE)) {
        doPrint("(unknown)");
      }
    }

    if (frame.location.hasMainFile && !(options_ & TERSE_FILE_AND_LINE)) {
      char mainFileBuf[kPathMax];
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

void SymbolizePrinter::color(SymbolizePrinter::Color color) {
  if ((options_ & COLOR) == 0 && ((options_ & COLOR_IF_TTY) == 0 || !isTty_)) {
    return;
  }
  if (static_cast<size_t>(color) >= kColorMap.size()) { // catches underflow too
    return;
  }
  doPrint(kColorMap[color]);
}

void SymbolizePrinter::println(const SymbolizedFrame& frame) {
  print(frame);
  doPrint("\n");
}

void SymbolizePrinter::printTerse(const SymbolizedFrame& frame) {
  if (frame.found && frame.name && frame.name[0] != '\0') {
    char demangledBuf[2048] = {0};
    demangle(frame.name, demangledBuf, sizeof(demangledBuf));
    doPrint(demangledBuf[0] == '\0' ? frame.name : demangledBuf);
  } else {
    // Can't use sprintf, not async-signal-safe
    static_assert(sizeof(uintptr_t) <= 8, "huge uintptr_t?");
    char buf[] = "0x0000000000000000";
    char* end = buf + sizeof(buf) - 1 - (16 - 2 * sizeof(uintptr_t));
    char* p = end;
    *p-- = '\0';
    auto address = frame.addr;
    while (address != 0) {
      *p-- = kHexChars[address & 0xf];
      address >>= 4;
    }
    doPrint(StringPiece(buf, end));
  }
}

void SymbolizePrinter::println(
    const SymbolizedFrame* frames, size_t frameCount) {
  for (size_t i = 0; i < frameCount; ++i) {
    println(frames[i]);
  }
}

namespace {

int getFD(const std::ios& stream) {
#if FOLLY_USE_LIBSTDCPP && FOLLY_HAS_RTTI
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
#else
  (void)stream;
#endif // __GNUC__
  return -1;
}

bool isColorfulTty(int options, int fd) {
  if ((options & SymbolizePrinter::TERSE) != 0 ||
      (options & SymbolizePrinter::COLOR_IF_TTY) == 0 || fd < 0 ||
      !::isatty(fd)) {
    return false;
  }
  auto term = ::getenv("TERM");
  return !(term == nullptr || term[0] == '\0' || strcmp(term, "dumb") == 0);
}

} // namespace

OStreamSymbolizePrinter::OStreamSymbolizePrinter(std::ostream& out, int options)
    : SymbolizePrinter(options, isColorfulTty(options, getFD(out))),
      out_(out) {}

void OStreamSymbolizePrinter::doPrint(StringPiece sp) {
  out_ << sp;
}

FDSymbolizePrinter::FDSymbolizePrinter(int fd, int options, size_t bufferSize)
    : SymbolizePrinter(options, isColorfulTty(options, fd)),
      fd_(fd),
      buffer_(bufferSize ? IOBuf::create(bufferSize) : nullptr) {}

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
    : SymbolizePrinter(options, isColorfulTty(options, fileno(file))),
      file_(file) {}

void FILESymbolizePrinter::doPrint(StringPiece sp) {
  fwrite(sp.data(), 1, sp.size(), file_);
}

void StringSymbolizePrinter::doPrint(StringPiece sp) {
  buf_.append(sp.data(), sp.size());
}

} // namespace symbolizer
} // namespace folly
