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

namespace folly {
namespace symbolizer {

/**
 * Address information: symbol name and location.
 *
 * Note that both name and location are references in the Symbolizer object,
 * which must outlive this AddressInfo object.
 */
struct AddressInfo {
  /* implicit */ AddressInfo(uintptr_t a=0, bool sf=false)
    : address(a),
      isSignalFrame(sf),
      found(false) { }
  uintptr_t address;
  bool isSignalFrame;
  bool found;
  StringPiece name;
  Dwarf::LocationInfo location;
};

/**
 * Get the current stack trace into addresses, which has room for at least
 * maxAddresses entries. Skip the first (topmost) skip entries.
 * Returns the number of entries in addresses on success, -1 on failure.
 */
ssize_t getStackTrace(AddressInfo* addresses,
                      size_t maxAddresses,
                      size_t skip=0);

class Symbolizer {
 public:
  Symbolizer() : fileCount_(0) { }

  /**
   * Symbolize given addresses.
   */
  void symbolize(AddressInfo* addresses, size_t addressCount);

  /**
   * Shortcut to symbolize one address.
   */
  bool symbolize(AddressInfo& address) {
    symbolize(&address, 1);
    return address.found;
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
  void print(const AddressInfo& ainfo);
  void print(const AddressInfo* addresses, size_t addressCount);

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

/**
 * Print an AddressInfo to a stream. Note that the Symbolizer that
 * symbolized the address must outlive the AddressInfo. Just like
 * OStreamSymbolizePrinter (which it uses internally), this is not
 * reentrant; do not use from signal handling code.
 */
std::ostream& operator<<(std::ostream& out, const AddressInfo& ainfo);

}  // namespace symbolizer
}  // namespace folly

#endif /* FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_ */

