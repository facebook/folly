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


#ifndef FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_
#define FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_

#include <cstdint>
#include <string>
#include <unordered_map>

#include "folly/Range.h"
#include "folly/experimental/symbolizer/Elf.h"
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
template <size_t N>
bool getStackTrace(FrameArray<N>& fa) {
  ssize_t n = getStackTrace(fa.addresses, N);
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

class Symbolizer {
 public:
  Symbolizer() : fileCount_(0) { }

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
  // We can't allocate memory, so we'll preallocate room.
  // "1023 shared libraries should be enough for everyone"
  static constexpr size_t kMaxFiles = 1024;
  size_t fileCount_;
  ElfFile files_[kMaxFiles];
};

/**
 * Print a list of symbolized addresses. Base class.
 */
class SymbolizePrinter {
 public:
  void print(uintptr_t address, const SymbolizedFrame& frame);
  void print(const uintptr_t* addresses,
             const SymbolizedFrame* frames,
             size_t frameCount);

  template <size_t N>
  void print(const FrameArray<N>& fa, size_t skip=0) {
    if (skip < fa.frameCount) {
      print(fa.addresses + skip, fa.frames + skip, fa.frameCount - skip);
    }
  }

  virtual ~SymbolizePrinter() { }
 private:
  virtual void doPrint(StringPiece sp) = 0;
};

/**
 * Print a list of symbolized addresses to a stream.
 * Not reentrant. Do not use from signal handling code.
 */
class OStreamSymbolizePrinter : public SymbolizePrinter {
 public:
  explicit OStreamSymbolizePrinter(std::ostream& out) : out_(out) { }
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
  explicit FDSymbolizePrinter(int fd) : fd_(fd) { }
 private:
  void doPrint(StringPiece sp) override;
  int fd_;
};

}  // namespace symbolizer
}  // namespace folly

#endif /* FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_ */

