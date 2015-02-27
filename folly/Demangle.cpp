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

#include <folly/Demangle.h>

#include <algorithm>
#include <string.h>

#include <folly/Malloc.h>

#if FOLLY_HAVE_CPLUS_DEMANGLE_V3_CALLBACK
# include <cxxabi.h>

// From libiberty
//
// TODO(tudorb): Detect this with autoconf for the open-source version.
//
// __attribute__((__weak__)) doesn't work, because cplus_demangle_v3_callback
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

namespace folly {

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

} // folly
