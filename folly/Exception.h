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

#include <errno.h>

#include <cstdio>
#include <stdexcept>
#include <system_error>

#include <folly/Conv.h>
#include <folly/FBString.h>
#include <folly/Likely.h>
#include <folly/Portability.h>

namespace folly {

// Various helpers to throw appropriate std::system_error exceptions from C
// library errors (returned in errno, as positive return values (many POSIX
// functions), or as negative return values (Linux syscalls))
//
// The *Explicit functions take an explicit value for errno.

// On linux and similar platforms the value of `errno` is a mixture of
// POSIX-`errno`-domain error codes and per-OS extended error codes. So the
// most appropriate category to use is `system_category`.
//
// On Windows `system_category` means codes that can be returned by Win32 API
// `GetLastError` and codes from the `errno`-domain must be reported as
// `generic_category`.
inline const std::error_category& errorCategoryForErrnoDomain() noexcept {
  if (kIsWindows) {
    return std::generic_category();
  }
  return std::system_category();
}

inline std::system_error makeSystemErrorExplicit(int err, const char* msg) {
  return std::system_error(err, errorCategoryForErrnoDomain(), msg);
}

template <class... Args>
std::system_error makeSystemErrorExplicit(int err, Args&&... args) {
  return makeSystemErrorExplicit(
      err, to<fbstring>(std::forward<Args>(args)...).c_str());
}

inline std::system_error makeSystemError(const char* msg) {
  return makeSystemErrorExplicit(errno, msg);
}

template <class... Args>
std::system_error makeSystemError(Args&&... args) {
  return makeSystemErrorExplicit(errno, std::forward<Args>(args)...);
}

// Helper to throw std::system_error
[[noreturn]] inline void throwSystemErrorExplicit(int err, const char* msg) {
  throw_exception(makeSystemErrorExplicit(err, msg));
}

template <class... Args>
[[noreturn]] void throwSystemErrorExplicit(int err, Args&&... args) {
  throw_exception(makeSystemErrorExplicit(err, std::forward<Args>(args)...));
}

// Helper to throw std::system_error from errno and components of a string
template <class... Args>
[[noreturn]] void throwSystemError(Args&&... args) {
  throwSystemErrorExplicit(errno, std::forward<Args>(args)...);
}

// Check a Posix return code (0 on success, error number on error), throw
// on error.
template <class... Args>
void checkPosixError(int err, Args&&... args) {
  if (UNLIKELY(err != 0)) {
    throwSystemErrorExplicit(err, std::forward<Args>(args)...);
  }
}

// Check a Linux kernel-style return code (>= 0 on success, negative error
// number on error), throw on error.
template <class... Args>
void checkKernelError(ssize_t ret, Args&&... args) {
  if (UNLIKELY(ret < 0)) {
    throwSystemErrorExplicit(int(-ret), std::forward<Args>(args)...);
  }
}

// Check a traditional Unix return code (-1 and sets errno on error), throw
// on error.
template <class... Args>
void checkUnixError(ssize_t ret, Args&&... args) {
  if (UNLIKELY(ret == -1)) {
    throwSystemError(std::forward<Args>(args)...);
  }
}

template <class... Args>
void checkUnixErrorExplicit(ssize_t ret, int savedErrno, Args&&... args) {
  if (UNLIKELY(ret == -1)) {
    throwSystemErrorExplicit(savedErrno, std::forward<Args>(args)...);
  }
}

// Check the return code from a fopen-style function (returns a non-nullptr
// FILE* on success, nullptr on error, sets errno).  Works with fopen, fdopen,
// freopen, tmpfile, etc.
template <class... Args>
void checkFopenError(FILE* fp, Args&&... args) {
  if (UNLIKELY(!fp)) {
    throwSystemError(std::forward<Args>(args)...);
  }
}

template <class... Args>
void checkFopenErrorExplicit(FILE* fp, int savedErrno, Args&&... args) {
  if (UNLIKELY(!fp)) {
    throwSystemErrorExplicit(savedErrno, std::forward<Args>(args)...);
  }
}

/**
 * If cond is not true, raise an exception of type E.  E must have a ctor that
 * works with const char* (a description of the failure).
 */
#define CHECK_THROW(cond, E)                             \
  do {                                                   \
    if (!(cond)) {                                       \
      folly::throw_exception<E>("Check failed: " #cond); \
    }                                                    \
  } while (0)

} // namespace folly
