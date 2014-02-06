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

#ifndef FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_
#define FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_

#include <cstdint>
#include <string>
#include <unordered_map>

#include "folly/FBString.h"
#include "folly/Range.h"
#include "folly/String.h"
#include "folly/experimental/symbolizer/Elf.h"
#include "folly/experimental/symbolizer/ElfCache.h"
#include "folly/experimental/symbolizer/Dwarf.h"
#include "folly/experimental/symbolizer/StackTrace.h"

namespace folly {
namespace symbolizer {

/**
 * Frame information: symbol name and location.
 *
 * Note that both name and location are references in the Symbolizer object,
 * which must outlive this SymbolizedFrame object.
 */
struct SymbolizedFrame {
  SymbolizedFrame() : found(false) { }
  bool isSignalFrame;
  bool found;
  StringPiece name;
  Dwarf::LocationInfo location;

  /**
   * Demangle the name and return it. Not async-signal-safe; allocates memory.
   */
  fbstring demangledName() const {
    return demangle(name.fbstr().c_str());
  }
};

template <size_t N>
struct FrameArray {
  FrameArray() : frameCount(0) { }

  size_t frameCount;
  uintptr_t addresses[N];
  SymbolizedFrame frames[N];
};

/**
 * Get stack trace into a given FrameArray, return true on success (and
 * set frameCount to the actual frame count, which may be > N) and false
 * on failure.
 */
namespace detail {
template <size_t N>
bool fixFrameArray(FrameArray<N>& fa, ssize_t n) {
  if (n != -1) {
    fa.frameCount = n;
    for (size_t i = 0; i < fa.frameCount; ++i) {
      fa.frames[i].found = false;
    }
    return true;
  } else {
    fa.frameCount = 0;
    return false;
  }
}
}  // namespace detail

// Always inline these functions; they don't do much, and unittests rely
// on them never showing up in a stack trace.
template <size_t N>
inline bool getStackTrace(FrameArray<N>& fa) __attribute__((always_inline));

template <size_t N>
inline bool getStackTrace(FrameArray<N>& fa) {
  return detail::fixFrameArray(fa, getStackTrace(fa.addresses, N));
}
template <size_t N>
inline bool getStackTraceSafe(FrameArray<N>& fa) __attribute__((always_inline));

template <size_t N>
inline bool getStackTraceSafe(FrameArray<N>& fa) {
  return detail::fixFrameArray(fa, getStackTraceSafe(fa.addresses, N));
}

class Symbolizer {
 public:
  explicit Symbolizer(ElfCacheBase* cache = nullptr);

  /**
   * Symbolize given addresses.
   */
  void symbolize(const uintptr_t* addresses,
                 SymbolizedFrame* frames,
                 size_t frameCount);

  template <size_t N>
  void symbolize(FrameArray<N>& fa) {
    symbolize(fa.addresses, fa.frames, fa.frameCount);
  }

  /**
   * Shortcut to symbolize one address.
   */
  bool symbolize(uintptr_t address, SymbolizedFrame& frame) {
    symbolize(&address, &frame, 1);
    return frame.found;
  }

 private:
  ElfCacheBase* cache_;
};

/**
 * Print a list of symbolized addresses. Base class.
 */
class SymbolizePrinter {
 public:
  /**
   * Print one address, no ending newline.
   */
  void print(uintptr_t address, const SymbolizedFrame& frame);

  /**
   * Print one address with ending newline.
   */
  void println(uintptr_t address, const SymbolizedFrame& frame);

  /**
   * Print multiple addresses on separate lines.
   */
  void println(const uintptr_t* addresses,
               const SymbolizedFrame* frames,
               size_t frameCount);

  /**
   * Print multiple addresses on separate lines, skipping the first
   * skip addresses.
   */
  template <size_t N>
  void println(const FrameArray<N>& fa, size_t skip=0) {
    if (skip < fa.frameCount) {
      println(fa.addresses + skip, fa.frames + skip, fa.frameCount - skip);
    }
  }

  virtual ~SymbolizePrinter() { }

  enum Options {
    // Skip file and line information
    NO_FILE_AND_LINE = 1 << 0,

    // As terse as it gets: function name if found, address otherwise
    TERSE = 1 << 1,

    // Always colorize output (ANSI escape code)
    COLOR = 1 << 2,

    // Colorize output only if output is printed to a TTY (ANSI escape code)
    COLOR_IF_TTY = 1 << 3,
  };

  enum Color { DEFAULT, RED, GREEN, YELLOW, BLUE, CYAN, WHITE, PURPLE };
  void color(Color c);

 protected:
  explicit SymbolizePrinter(int options, bool isTty = false)
    : options_(options),
      isTty_(isTty) {
  }

  const int options_;
  const bool isTty_;

 private:
  void printTerse(uintptr_t address, const SymbolizedFrame& frame);
  virtual void doPrint(StringPiece sp) = 0;
};

/**
 * Print a list of symbolized addresses to a stream.
 * Not reentrant. Do not use from signal handling code.
 */
class OStreamSymbolizePrinter : public SymbolizePrinter {
 public:
  explicit OStreamSymbolizePrinter(std::ostream& out, int options=0);
 private:
  void doPrint(StringPiece sp) override;
  std::ostream& out_;
};

/**
 * Print a list of symbolized addresses to a file descriptor.
 * Ignores errors. Async-signal-safe.
 */
class FDSymbolizePrinter : public SymbolizePrinter {
 public:
  explicit FDSymbolizePrinter(int fd, int options=0);
 private:
  void doPrint(StringPiece sp) override;
  int fd_;
};

/**
 * Print a list of symbolized addresses to a FILE*.
 * Ignores errors. Not reentrant. Do not use from signal handling code.
 */
class FILESymbolizePrinter : public SymbolizePrinter {
 public:
  explicit FILESymbolizePrinter(FILE* file, int options=0);
 private:
  void doPrint(StringPiece sp) override;
  FILE* file_;
};

/**
 * Print a list of symbolized addresses to a std::string.
 * Not reentrant. Do not use from signal handling code.
 */
class StringSymbolizePrinter : public SymbolizePrinter {
 public:
  explicit StringSymbolizePrinter(int options=0) : SymbolizePrinter(options) { }

  std::string str() const { return buf_.toStdString(); }
  const fbstring& fbstr() const { return buf_; }
  fbstring moveFbString() { return std::move(buf_); }

 private:
  void doPrint(StringPiece sp) override;
  fbstring buf_;
};

}  // namespace symbolizer
}  // namespace folly

#endif /* FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_ */
