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

#include <cassert>

#include <folly/detail/FileUtilDetail.h>
#include <folly/portability/Config.h>

namespace folly {
namespace fileutil_detail {
namespace {
std::string getTemporaryFilePathStringWithoutTempDirectory(
    const std::string& filePath);

std::string getTemporaryFilePathStringWithTemporaryDirectory(
    const std::string& temporaryDirectory);
} // namespace

std::string getTemporaryFilePathString(
    const std::string& filePath, const std::string& temporaryDirectory) {
  return temporaryDirectory.empty()
      ? getTemporaryFilePathStringWithoutTempDirectory(filePath)
      : getTemporaryFilePathStringWithTemporaryDirectory(temporaryDirectory);
}

namespace {
std::string getTemporaryFilePathStringWithoutTempDirectory(
    const std::string& filePath) {
  return filePath + std::string{".XXXXXX"};
}

std::string getTemporaryFilePathStringWithTemporaryDirectory(
    const std::string& temporaryDirectory) {
#if !defined(_WIN32) && !FOLLY_MOBILE
  return (temporaryDirectory.back() == '/')
      ? (temporaryDirectory + std::string{"tempForAtomicWrite.XXXXXX"})
      : (temporaryDirectory + std::string{"/tempForAtomicWrite.XXXXXX"});
#else
  // The implementation currently does not support win32 or mobile
  // for temporary directory based atomic file writes.
  static_cast<void>(temporaryDirectory);
  assert(false);

  // return needed to silence -Werror=return-type errors on some builds
  return std::string{};
#endif
}
} // namespace
} // namespace fileutil_detail
} // namespace folly
