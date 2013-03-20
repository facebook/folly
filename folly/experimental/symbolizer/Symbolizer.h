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
 * Convert an address to symbol name and source location.
 */
class Symbolizer {
 public:
  /**
   * Symbolize an instruction pointer address, returning the symbol name
   * and file/line number information.
   *
   * The returned StringPiece objects are valid for the lifetime of
   * this Symbolizer object.
   */
  bool symbolize(uintptr_t address, folly::StringPiece& symbolName,
                 Dwarf::LocationInfo& location);

  static void write(std::ostream& out, uintptr_t address,
                    folly::StringPiece symbolName,
                    const Dwarf::LocationInfo& location);

 private:
  ElfFile& getFile(const std::string& name);
  // cache open ELF files
  std::unordered_map<std::string, ElfFile> elfFiles_;
};

}  // namespace symbolizer
}  // namespace folly

#endif /* FOLLY_EXPERIMENTAL_SYMBOLIZER_SYMBOLIZER_H_ */

