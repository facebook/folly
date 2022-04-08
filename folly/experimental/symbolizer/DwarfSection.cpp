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

#include <folly/experimental/symbolizer/DwarfSection.h>

#include <folly/experimental/symbolizer/DwarfUtil.h>

#if FOLLY_HAVE_DWARF && FOLLY_HAVE_ELF

namespace folly {
namespace symbolizer {

DwarfSection::DwarfSection(folly::StringPiece d) : is64Bit_(false), data_(d) {}

// Next chunk in section
bool DwarfSection::next(folly::StringPiece& chunk) {
  chunk = data_;
  if (chunk.empty()) {
    return false;
  }

  // Initial length is a uint32_t value for a 32-bit section, and
  // a 96-bit value (0xffffffff followed by the 64-bit length) for a 64-bit
  // section.
  auto initialLength = read<uint32_t>(chunk);
  is64Bit_ = (initialLength == uint32_t(-1));
  auto length = is64Bit_ ? read<uint64_t>(chunk) : initialLength;
  if (length > chunk.size()) {
    FOLLY_SAFE_DFATAL(
        "invalid DWARF section, length: ",
        length,
        " chunk.size(): ",
        chunk.size());
    return false;
  }
  chunk.reset(chunk.data(), length);
  data_.assign(chunk.end(), data_.end());
  return true;
}

} // namespace symbolizer
} // namespace folly

#endif // FOLLY_HAVE_DWARF
