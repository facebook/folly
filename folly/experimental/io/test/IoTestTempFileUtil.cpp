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

#include <folly/experimental/io/test/IoTestTempFileUtil.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <random>

namespace folly {
namespace test {

TemporaryFile TempFileUtil::getTempFile(size_t size) {
  CHECK_EQ(size % sizeof(uint32_t), 0);
  size /= sizeof(uint32_t);

  TemporaryFile tmpFile;
  int fd = tmpFile.fd();
  CHECK_GE(fd, 0);

  // fill the file the file with random data
  const uint32_t seed = 42;
  std::mt19937 rnd(seed);

  constexpr size_t bufferSize = 1U << 16;
  uint32_t buffer[bufferSize];

  while (size) {
    size_t n = std::min(size, bufferSize);
    for (size_t i = 0; i < n; ++i) {
      buffer[i] = rnd();
    }
    size_t written = folly::writeFull(fd, buffer, sizeof(uint32_t) * n);
    CHECK_EQ(written, sizeof(uint32_t) * n);
    size -= n;
  }

  CHECK_EQ(::fdatasync(fd), 0);

  // the file was opened with O_EXCL so we need to close to be able
  // to open it again
  tmpFile.close();

  return tmpFile;
}

} // namespace test
} // namespace folly
