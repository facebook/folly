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

#include "folly/String.h"
#include "folly/Format.h"

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <stdexcept>
#include <iterator>
#include <glog/logging.h>

#if FOLLY_HAVE_CPLUS_DEMANGLE_V3_CALLBACK
# include <cxxabi.h>

// From libiberty
//
// TODO(tudorb): Detect this with autoconf for the open-source version.
//
// __attribute__((weak)) doesn't work, because cplus_demangle_v3_callback
// is exported by an object file in libiberty.a, and the ELF spec says
// "The link editor does not extract archive members to resolve undefined weak
// symbols" (but, interestingly enough, will resolve undefined weak symbols
// with definitions from archive members that were extracted in order to
// resolve an undefined global (strong) symbol)

# ifndef DMGL_NO_OPTS
#  define FOLLY_DEFINED_DMGL 1
#  define DMGL_NO_OPTS    0          /* For readability... */
#  define DMGL_PARAMS     (1 << 0)   /* Include function args */
#  define DMGL_ANSI       (1 << 1)   /* Include const, volatile, etc */
#  define DMGL_JAVA       (1 << 2)   /* Demangle as Java rather than C++. */
#  define DMGL_VERBOSE    (1 << 3)   /* Include implementation details.  */
#  define DMGL_TYPES      (1 << 4)   /* Also try to demangle type encodings.  */
#  define DMGL_RET_POSTFIX (1 << 5)  /* Print function return types (when
                                        present) after function signature */
# endif

extern "C" int cplus_demangle_v3_callback(
    const char* mangled,
    int options,  // We use DMGL_PARAMS | DMGL_TYPES, aka 0x11
    void (*callback)(const char*, size_t, void*),
    void* arg);

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

  // https://developer.apple.com/library/mac/documentation/Darwin/Reference/ManPages/man3/strerror_r.3.html
  // http://www.kernel.org/doc/man-pages/online/pages/man3/strerror.3.html
#if defined(__APPLE__) || defined(__FreeBSD__) || \
    ((_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !_GNU_SOURCE)
  // Using XSI-compatible strerror_r
  int r = strerror_r(err, buf, sizeof(buf));

  // OSX/FreeBSD use EINVAL and Linux uses -1 so just check for non-zero
  if (r != 0) {
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

namespace {

// glibc doesn't have strlcpy
size_t my_strlcpy(char* dest, const char* src, size_t size) {
  size_t len = strlen(src);
  if (size != 0) {
    size_t n = std::min(len, size - 1);  // always null terminate!
    memcpy(dest, src, n);
    dest[n] = '\0';
  }
  return len;
}

}  // namespace

#if FOLLY_HAVE_CPLUS_DEMANGLE_V3_CALLBACK

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

namespace {

struct DemangleBuf {
  char* dest;
  size_t remaining;
  size_t total;
};

void demangleCallback(const char* str, size_t size, void* p) {
  DemangleBuf* buf = static_cast<DemangleBuf*>(p);
  size_t n = std::min(buf->remaining, size);
  memcpy(buf->dest, str, n);
  buf->dest += n;
  buf->remaining -= n;
  buf->total += size;
}

}  // namespace

size_t demangle(const char* name, char* out, size_t outSize) {
  DemangleBuf dbuf;
  dbuf.dest = out;
  dbuf.remaining = outSize ? outSize - 1 : 0;   // leave room for null term
  dbuf.total = 0;

  // Unlike most library functions, this returns 1 on success and 0 on failure
  int status = cplus_demangle_v3_callback(
      name,
      DMGL_PARAMS | DMGL_ANSI | DMGL_TYPES,
      demangleCallback,
      &dbuf);
  if (status == 0) {  // failed, return original
    return my_strlcpy(out, name, outSize);
  }
  if (outSize != 0) {
    *dbuf.dest = '\0';
  }
  return dbuf.total;
}

#else

fbstring demangle(const char* name) {
  return name;
}

size_t demangle(const char* name, char* out, size_t outSize) {
  return my_strlcpy(out, name, outSize);
}

#endif

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

#ifdef FOLLY_DEFINED_DMGL
# undef FOLLY_DEFINED_DMGL
# undef DMGL_NO_OPTS
# undef DMGL_PARAMS
# undef DMGL_ANSI
# undef DMGL_JAVA
# undef DMGL_VERBOSE
# undef DMGL_TYPES
# undef DMGL_RET_POSTFIX
#endif

