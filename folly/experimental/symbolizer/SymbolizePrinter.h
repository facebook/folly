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

#pragma once

#include <cstdint>

#include <folly/FBString.h>
#include <folly/Range.h>
#include <folly/experimental/symbolizer/SymbolizedFrame.h>

namespace folly {
class IOBuf;

namespace symbolizer {

/**
 * Format one address in the way it's usually printed by SymbolizePrinter.
 * Async-signal-safe.
 */
class AddressFormatter {
 public:
  AddressFormatter();

  /**
   * Format the address. Returns an internal buffer.
   */
  StringPiece format(uintptr_t address);

 private:
  static constexpr char bufTemplate[] = "    @ 0000000000000000";
  char buf_[sizeof(bufTemplate)];
};

/**
 * Print a list of symbolized addresses. Base class.
 */
class SymbolizePrinter {
 public:
  /**
   * Print one frame, no ending newline.
   */
  void print(const SymbolizedFrame& frame);

  /**
   * Print one frame with ending newline.
   */
  void println(const SymbolizedFrame& frame);

  /**
   * Print multiple frames on separate lines.
   */
  void println(const SymbolizedFrame* frames, size_t frameCount);

  /**
   * Print a string, no endling newline.
   */
  void print(StringPiece sp) { doPrint(sp); }

  /**
   * Print multiple frames on separate lines, skipping the first
   * skip addresses.
   */
  template <size_t N>
  void println(const FrameArray<N>& fa, size_t skip = 0) {
    if (skip < fa.frameCount) {
      println(fa.frames + skip, fa.frameCount - skip);
    }
  }

  /**
   * If output buffered inside this class, send it to the output stream, so that
   * any output done in other ways appears after this.
   */
  virtual void flush() {}

  virtual ~SymbolizePrinter() {}

  enum Options {
    // Skip file and line information
    NO_FILE_AND_LINE = 1 << 0,

    // As terse as it gets: function name if found, address otherwise
    TERSE = 1 << 1,

    // Always colorize output (ANSI escape code)
    COLOR = 1 << 2,

    // Colorize output only if output is printed to a TTY (ANSI escape code)
    COLOR_IF_TTY = 1 << 3,

    // Skip frame address information
    NO_FRAME_ADDRESS = 1 << 4,

    // Simple file and line output
    TERSE_FILE_AND_LINE = 1 << 5,
  };

  // NOTE: enum values used as indexes in kColorMap.
  enum Color { DEFAULT, RED, GREEN, YELLOW, BLUE, CYAN, WHITE, PURPLE, NUM };
  void color(Color c);

 protected:
  explicit SymbolizePrinter(int options, bool isTty = false)
      : options_(options), isTty_(isTty) {}

  const int options_;
  const bool isTty_;

 private:
  void printTerse(const SymbolizedFrame& frame);
  virtual void doPrint(StringPiece sp) = 0;

  static constexpr std::array<const char*, Color::NUM> kColorMap = {{
      "\x1B[0m",
      "\x1B[31m",
      "\x1B[32m",
      "\x1B[33m",
      "\x1B[34m",
      "\x1B[36m",
      "\x1B[37m",
      "\x1B[35m",
  }};
};

/**
 * Print a list of symbolized addresses to a stream.
 * Not reentrant. Do not use from signal handling code.
 */
class OStreamSymbolizePrinter : public SymbolizePrinter {
 public:
  explicit OStreamSymbolizePrinter(std::ostream& out, int options = 0);

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
  explicit FDSymbolizePrinter(int fd, int options = 0, size_t bufferSize = 0);
  ~FDSymbolizePrinter() override;
  virtual void flush() override;

 private:
  void doPrint(StringPiece sp) override;

  const int fd_;
  std::unique_ptr<IOBuf> buffer_;
};

/**
 * Print a list of symbolized addresses to a FILE*.
 * Ignores errors. Not reentrant. Do not use from signal handling code.
 */
class FILESymbolizePrinter : public SymbolizePrinter {
 public:
  explicit FILESymbolizePrinter(FILE* file, int options = 0);

 private:
  void doPrint(StringPiece sp) override;
  FILE* const file_ = nullptr;
};

/**
 * Print a list of symbolized addresses to a std::string.
 * Not reentrant. Do not use from signal handling code.
 */
class StringSymbolizePrinter : public SymbolizePrinter {
 public:
  explicit StringSymbolizePrinter(int options = 0)
      : SymbolizePrinter(options) {}

  std::string str() const { return buf_.toStdString(); }
  const fbstring& fbstr() const { return buf_; }
  fbstring moveFbString() { return std::move(buf_); }

 private:
  void doPrint(StringPiece sp) override;
  fbstring buf_;
};

} // namespace symbolizer
} // namespace folly
