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

#include "folly/String.h"
#include "folly/Format.h"

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <stdexcept>
#include <iterator>
#include <glog/logging.h>

#undef FOLLY_DEMANGLE
#if defined(__GNUG__) && __GNUG__ >= 4
# include <cxxabi.h>
# define FOLLY_DEMANGLE 1
#endif

namespace folly {

namespace {

inline void stringPrintfImpl(std::string& output, const char* format,
                             va_list args) {
  // Tru to the space at the end of output for our output buffer.
  // Find out write point then inflate its size temporarily to its
  // capacity; we will later shrink it to the size needed to represent
  // the formatted string.  If this buffer isn't large enough, we do a
  // resize and try again.

  const auto write_point = output.size();
  auto remaining = output.capacity() - write_point;
  output.resize(output.capacity());

  va_list args_copy;
  va_copy(args_copy, args);
  int bytes_used = vsnprintf(&output[write_point], remaining, format,
                             args_copy);
  va_end(args_copy);
  if (bytes_used < 0) {
    throw std::runtime_error(
      to<std::string>("Invalid format string; snprintf returned negative "
                      "with format string: ", format));
  } else if (bytes_used < remaining) {
    // There was enough room, just shrink and return.
    output.resize(write_point + bytes_used);
  } else {
    output.resize(write_point + bytes_used + 1);
    remaining = bytes_used + 1;
    va_list args_copy;
    va_copy(args_copy, args);
    bytes_used = vsnprintf(&output[write_point], remaining, format,
                           args_copy);
    va_end(args_copy);
    if (bytes_used + 1 != remaining) {
      throw std::runtime_error(
        to<std::string>("vsnprint retry did not manage to work "
                        "with format string: ", format));
    }
    output.resize(write_point + bytes_used);
  }
}

}  // anon namespace

std::string stringPrintf(const char* format, ...) {
  // snprintf will tell us how large the output buffer should be, but
  // we then have to call it a second time, which is costly.  By
  // guestimating the final size, we avoid the double snprintf in many
  // cases, resulting in a performance win.  We use this constructor
  // of std::string to avoid a double allocation, though it does pad
  // the resulting string with nul bytes.  Our guestimation is twice
  // the format string size, or 32 bytes, whichever is larger.  This
  // is a hueristic that doesn't affect correctness but attempts to be
  // reasonably fast for the most common cases.
  std::string ret(std::max(32UL, strlen(format) * 2), '\0');
  ret.resize(0);

  va_list ap;
  va_start(ap, format);
  stringPrintfImpl(ret, format, ap);
  va_end(ap);
  return ret;
}

// Basic declarations; allow for parameters of strings and string
// pieces to be specified.
std::string& stringAppendf(std::string* output, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  stringPrintfImpl(*output, format, ap);
  va_end(ap);
  return *output;
}

void stringPrintf(std::string* output, const char* format, ...) {
  output->clear();
  va_list ap;
  va_start(ap, format);
  stringPrintfImpl(*output, format, ap);
  va_end(ap);
};

namespace {

struct PrettySuffix {
  const char* suffix;
  double val;
};

const PrettySuffix kPrettyTimeSuffixes[] = {
  { "s ", 1e0L },
  { "ms", 1e-3L },
  { "us", 1e-6L },
  { "ns", 1e-9L },
  { "ps", 1e-12L },
  { "s ", 0 },
  { 0, 0 },
};

const PrettySuffix kPrettyBytesMetricSuffixes[] = {
  { "TB", 1e12L },
  { "GB", 1e9L },
  { "MB", 1e6L },
  { "kB", 1e3L },
  { "B ", 0L },
  { 0, 0 },
};

const PrettySuffix kPrettyBytesBinarySuffixes[] = {
  { "TB", int64_t(1) << 40 },
  { "GB", int64_t(1) << 30 },
  { "MB", int64_t(1) << 20 },
  { "kB", int64_t(1) << 10 },
  { "B ", 0L },
  { 0, 0 },
};

const PrettySuffix kPrettyBytesBinaryIECSuffixes[] = {
  { "TiB", int64_t(1) << 40 },
  { "GiB", int64_t(1) << 30 },
  { "MiB", int64_t(1) << 20 },
  { "KiB", int64_t(1) << 10 },
  { "B  ", 0L },
  { 0, 0 },
};

const PrettySuffix kPrettyUnitsMetricSuffixes[] = {
  { "tril", 1e12L },
  { "bil",  1e9L },
  { "M",    1e6L },
  { "k",    1e3L },
  { " ",      0  },
  { 0, 0 },
};

const PrettySuffix kPrettyUnitsBinarySuffixes[] = {
  { "T", int64_t(1) << 40 },
  { "G", int64_t(1) << 30 },
  { "M", int64_t(1) << 20 },
  { "k", int64_t(1) << 10 },
  { " ", 0 },
  { 0, 0 },
};

const PrettySuffix kPrettyUnitsBinaryIECSuffixes[] = {
  { "Ti", int64_t(1) << 40 },
  { "Gi", int64_t(1) << 30 },
  { "Mi", int64_t(1) << 20 },
  { "Ki", int64_t(1) << 10 },
  { "  ", 0 },
  { 0, 0 },
};

const PrettySuffix* const kPrettySuffixes[PRETTY_NUM_TYPES] = {
  kPrettyTimeSuffixes,
  kPrettyBytesMetricSuffixes,
  kPrettyBytesBinarySuffixes,
  kPrettyBytesBinaryIECSuffixes,
  kPrettyUnitsMetricSuffixes,
  kPrettyUnitsBinarySuffixes,
  kPrettyUnitsBinaryIECSuffixes,
};

}  // namespace

std::string prettyPrint(double val, PrettyType type, bool addSpace) {
  char buf[100];

  // pick the suffixes to use
  assert(type >= 0);
  assert(type < PRETTY_NUM_TYPES);
  const PrettySuffix* suffixes = kPrettySuffixes[type];

  // find the first suffix we're bigger than -- then use it
  double abs_val = fabs(val);
  for (int i = 0; suffixes[i].suffix; ++i) {
    if (abs_val >= suffixes[i].val) {
      snprintf(buf, sizeof buf, "%.4g%s%s",
               (suffixes[i].val ? (val / suffixes[i].val)
                                : val),
               (addSpace ? " " : ""),
               suffixes[i].suffix);
      return std::string(buf);
    }
  }

  // no suffix, we've got a tiny value -- just print it in sci-notation
  snprintf(buf, sizeof buf, "%.4g", val);
  return std::string(buf);
}

std::string hexDump(const void* ptr, size_t size) {
  std::ostringstream os;
  hexDump(ptr, size, std::ostream_iterator<StringPiece>(os, "\n"));
  return os.str();
}

fbstring errnoStr(int err) {
  int savedErrno = errno;

  // Ensure that we reset errno upon exit.
  auto guard(makeGuard([&] { errno = savedErrno; }));

  char buf[1024];
  buf[0] = '\0';

  fbstring result;

  // http://www.kernel.org/doc/man-pages/online/pages/man3/strerror.3.html
#if (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !_GNU_SOURCE
  // Using XSI-compatible strerror_r
  int r = strerror_r(err, buf, sizeof(buf));

  if (r == -1) {
    result = to<fbstring>(
      "Unknown error ", err,
      " (strerror_r failed with error ", errno, ")");
  } else {
    result.assign(buf);
  }
#else
  // Using GNU strerror_r
  result.assign(strerror_r(err, buf, sizeof(buf)));
#endif

  return result;
}

#ifdef FOLLY_DEMANGLE

fbstring demangle(const char* name) {
  int status;
  size_t len = 0;
  // malloc() memory for the demangled type name
  char* demangled = abi::__cxa_demangle(name, nullptr, &len, &status);
  if (status != 0) {
    return name;
  }
  // len is the length of the buffer (including NUL terminator and maybe
  // other junk)
  return fbstring(demangled, strlen(demangled), len, AcquireMallocatedString());
}

#else

fbstring demangle(const char* name) {
  return name;
}

#endif
#undef FOLLY_DEMANGLE

namespace detail {

size_t hexDumpLine(const void* ptr, size_t offset, size_t size,
                   std::string& line) {
  // Line layout:
  // 8: address
  // 1: space
  // (1+2)*16: hex bytes, each preceded by a space
  // 1: space separating the two halves
  // 3: "  |"
  // 16: characters
  // 1: "|"
  // Total: 78
  line.clear();
  line.reserve(78);
  const uint8_t* p = reinterpret_cast<const uint8_t*>(ptr) + offset;
  size_t n = std::min(size - offset, size_t(16));
  format("{:08x} ", offset).appendTo(line);

  for (size_t i = 0; i < n; i++) {
    if (i == 8) {
      line.push_back(' ');
    }
    format(" {:02x}", p[i]).appendTo(line);
  }

  // 3 spaces for each byte we're not printing, one separating the halves
  // if necessary
  line.append(3 * (16 - n) + (n <= 8), ' ');
  line.append("  |");

  for (size_t i = 0; i < n; i++) {
    char c = (p[i] >= 32 && p[i] <= 126 ? static_cast<char>(p[i]) : '.');
    line.push_back(c);
  }
  line.append(16 - n, ' ');
  line.push_back('|');
  DCHECK_EQ(line.size(), 78);

  return n;
}

} // namespace detail

}   // namespace folly
