/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/Format.h>

namespace folly {
namespace detail {

extern const FormatArg::Align formatAlignTable[];
extern const FormatArg::Sign formatSignTable[];

}  // namespace detail

using namespace folly::detail;

void FormatArg::initSlow() {
  auto b = fullArgString.begin();
  auto end = fullArgString.end();

  // Parse key
  auto p = static_cast<const char*>(memchr(b, ':', end - b));
  if (!p) {
    key_ = StringPiece(b, end);
    return;
  }
  key_ = StringPiece(b, p);

  if (*p == ':') {
    // parse format spec
    if (++p == end) return;

    // fill/align, or just align
    Align a;
    if (p + 1 != end &&
        (a = formatAlignTable[static_cast<unsigned char>(p[1])]) !=
        Align::INVALID) {
      fill = *p;
      align = a;
      p += 2;
      if (p == end) return;
    } else if ((a = formatAlignTable[static_cast<unsigned char>(*p)]) !=
               Align::INVALID) {
      align = a;
      if (++p == end) return;
    }

    Sign s;
    unsigned char uSign = static_cast<unsigned char>(*p);
    if ((s = formatSignTable[uSign]) != Sign::INVALID) {
      sign = s;
      if (++p == end) return;
    }

    if (*p == '#') {
      basePrefix = true;
      if (++p == end) return;
    }

    if (*p == '0') {
      enforce(align == Align::DEFAULT, "alignment specified twice");
      fill = '0';
      align = Align::PAD_AFTER_SIGN;
      if (++p == end) return;
    }

    if (*p >= '0' && *p <= '9') {
      auto b = p;
      do {
        ++p;
      } while (p != end && *p >= '0' && *p <= '9');
      width = to<int>(StringPiece(b, p));

      if (p == end) return;
    }

    if (*p == ',') {
      thousandsSeparator = true;
      if (++p == end) return;
    }

    if (*p == '.') {
      auto b = ++p;
      while (p != end && *p >= '0' && *p <= '9') {
        ++p;
      }
      if (p != b) {
        precision = to<int>(StringPiece(b, p));
        if (p != end && *p == '.') {
          trailingDot = true;
          ++p;
        }
      } else {
        trailingDot = true;
      }

      if (p == end) return;
    }

    presentation = *p;
    if (++p == end) return;
  }

  error("extra characters in format string");
}

void FormatArg::validate(Type type) const {
  enforce(keyEmpty(), "index not allowed");
  switch (type) {
  case Type::INTEGER:
    enforce(precision == kDefaultPrecision,
            "precision not allowed on integers");
    break;
  case Type::FLOAT:
    enforce(!basePrefix,
            "base prefix ('#') specifier only allowed on integers");
    enforce(!thousandsSeparator,
            "thousands separator (',') only allowed on integers");
    break;
  case Type::OTHER:
    enforce(align != Align::PAD_AFTER_SIGN,
            "'='alignment only allowed on numbers");
    enforce(sign == Sign::DEFAULT,
            "sign specifier only allowed on numbers");
    enforce(!basePrefix,
            "base prefix ('#') specifier only allowed on integers");
    enforce(!thousandsSeparator,
            "thousands separator (',') only allowed on integers");
    break;
  }
}

namespace detail {
void insertThousandsGroupingUnsafe(char* start_buffer, char** end_buffer) {
  uint32_t remaining_digits = *end_buffer - start_buffer;
  uint32_t separator_size = (remaining_digits - 1) / 3;
  uint32_t result_size = remaining_digits + separator_size;
  *end_buffer = *end_buffer + separator_size;

  // get the end of the new string with the separators
  uint32_t buffer_write_index = result_size - 1;
  uint32_t buffer_read_index = remaining_digits - 1;
  start_buffer[buffer_write_index + 1] = 0;

  uint32_t count = 0;
  bool done = false;
  uint32_t next_group_size = 3;

  while (!done) {
    uint32_t current_group_size = std::max<uint32_t>(1,
      std::min<uint32_t>(remaining_digits, next_group_size));

    // write out the current group's digits to the buffer index
    for (uint32_t i = 0; i < current_group_size; i++) {
      start_buffer[buffer_write_index--] = start_buffer[buffer_read_index--];
    }

    // if not finished, write the separator before the next group
    if (buffer_write_index < buffer_write_index + 1) {
      start_buffer[buffer_write_index--] = ',';
    } else {
      done = true;
    }

    remaining_digits -= current_group_size;
  }
}
} // detail

}  // namespace folly
