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

#include <algorithm>
#include <cerrno>

#include <folly/detail/FileUtilDetail.h>
#include <folly/portability/SysUio.h>
#include <folly/portability/Unistd.h>

/**
 * Helper functions and templates for FileUtil.cpp.  Declared here so
 * they can be unittested.
 */
namespace folly {
namespace fileutil_detail {

// Retry reading/writing iovec objects.
// On POSIX platforms this wraps readv/preadv/writev/pwritev to
// retry on incomplete reads / writes.  On Windows this wraps
// read/pread/write/pwrite.  Note that pread and pwrite are not native calls on
// Windows and are provided by folly/portability/Unistd.cpp
template <class F, class... Offset>
ssize_t wrapvFull(F f, int fd, iovec* iov, int count, Offset... offset) {
  ssize_t totalBytes = 0;
  ssize_t r;
  do {
#ifndef _WIN32
    r = f(fd, iov, std::min<int>(count, kIovMax), offset...);
#else // _WIN32
    // On Windows the caller will pass in just the simple
    // read/write/pread/pwrite function, since the OS does not provide *v()
    // versions.
    r = f(fd, iov->iov_base, iov->iov_len, offset...);
#endif // _WIN32
    if (r == -1) {
      if (errno == EINTR) {
        continue;
      }
      return r;
    }

    if (r == 0) {
      break; // EOF
    }

    totalBytes += r;
    incr(r, offset...);
    while (r != 0 && count != 0) {
      if (r >= ssize_t(iov->iov_len)) {
        r -= ssize_t(iov->iov_len);
        ++iov;
        --count;
      } else {
        iov->iov_base = static_cast<char*>(iov->iov_base) + r;
        iov->iov_len -= r;
        r = 0;
      }
    }
  } while (count);

  return totalBytes;
}

} // namespace fileutil_detail
} // namespace folly
