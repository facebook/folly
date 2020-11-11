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

#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

#include <folly/Range.h>

namespace folly {
namespace symbolizer {

class ElfFile;

/**
 * Represent a file path as a collection of three parts (base directory,
 * subdirectory, and file).
 */
class Path {
 public:
  Path() = default;

  Path(
      folly::StringPiece baseDir,
      folly::StringPiece subDir,
      folly::StringPiece file);

  folly::StringPiece baseDir() const { return baseDir_; }
  folly::StringPiece subDir() const { return subDir_; }
  folly::StringPiece file() const { return file_; }

  size_t size() const;

  /**
   * Copy the Path to a buffer of size bufSize.
   *
   * toBuffer behaves like snprintf: It will always null-terminate the
   * buffer (so it will copy at most bufSize-1 bytes), and it will return
   * the number of bytes that would have been written if there had been
   * enough room, so, if toBuffer returns a value >= bufSize, the output
   * was truncated.
   */
  size_t toBuffer(char* buf, size_t bufSize) const;

  void toString(std::string& dest) const;
  std::string toString() const {
    std::string s;
    toString(s);
    return s;
  }

 private:
  folly::StringPiece baseDir_;
  folly::StringPiece subDir_;
  folly::StringPiece file_;
};

inline std::ostream& operator<<(std::ostream& out, const Path& path) {
  return out << path.toString();
}

enum class LocationInfoMode {
  // Don't resolve location info.
  DISABLED,
  // Perform CU lookup using .debug_aranges (might be incomplete).
  FAST,
  // Scan all CU in .debug_info (slow!) on .debug_aranges lookup failure.
  FULL,
  // Scan .debug_info (super slower, use with caution) for inline functions in
  // addition to FULL.
  FULL_WITH_INLINE,
};

/**
 * Contains location info like file name, line number, etc.
 */
struct LocationInfo {
  bool hasFileAndLine = false;
  bool hasMainFile = false;
  Path mainFile;
  Path file;
  uint64_t line = 0;
};

/**
 * Frame information: symbol name and location.
 */
struct SymbolizedFrame {
  bool found = false;
  uintptr_t addr = 0;
  // Mangled symbol name. Use `folly::demangle()` to demangle it.
  const char* name = nullptr;
  LocationInfo location;
  std::shared_ptr<ElfFile> file;

  void clear() { *this = SymbolizedFrame(); }
};

template <size_t N>
struct FrameArray {
  FrameArray() = default;

  size_t frameCount = 0;
  uintptr_t addresses[N];
  SymbolizedFrame frames[N];
};

} // namespace symbolizer
} // namespace folly
