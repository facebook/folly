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

#include <folly/experimental/io/test/AsyncBaseTestLib.h>

namespace folly {
namespace test {
void TestUtil::waitUntilReadable(int fd) {
  pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;

  int r;
  do {
    r = poll(&pfd, 1, -1); // wait forever
  } while (r == -1 && errno == EINTR);
  PCHECK(r == 1);
  CHECK_EQ(pfd.revents, POLLIN); // no errors etc
}

folly::Range<folly::AsyncBase::Op**> TestUtil::readerWait(
    folly::AsyncBase* reader) {
  int fd = reader->pollFd();
  if (fd == -1) {
    return reader->wait(1);
  } else {
    waitUntilReadable(fd);
    return reader->pollCompleted();
  }
}

TestUtil::ManagedBuffer TestUtil::allocateAligned(size_t size) {
  void* buf;
  int rc = posix_memalign(&buf, kAlign, size);
  CHECK_EQ(rc, 0) << folly::errnoStr(rc);
  return ManagedBuffer(reinterpret_cast<char*>(buf), free);
}

TemporaryFile::TemporaryFile(size_t size)
    : path_(folly::fs::temp_directory_path() / folly::fs::unique_path()) {
  CHECK_EQ(size % sizeof(uint32_t), 0);
  size /= sizeof(uint32_t);
  const uint32_t seed = 42;
  std::mt19937 rnd(seed);

  const size_t bufferSize = 1U << 16;
  uint32_t buffer[bufferSize];

  FILE* fp = ::fopen(path_.c_str(), "wb");
  PCHECK(fp != nullptr);
  while (size) {
    size_t n = std::min(size, bufferSize);
    for (size_t i = 0; i < n; ++i) {
      buffer[i] = rnd();
    }
    size_t written = ::fwrite(buffer, sizeof(uint32_t), n, fp);
    PCHECK(written == n);
    size -= written;
  }
  PCHECK(::fclose(fp) == 0);
}

TemporaryFile::~TemporaryFile() {
  try {
    folly::fs::remove(path_);
  } catch (const folly::fs::filesystem_error& e) {
    LOG(ERROR) << "fs::remove: " << folly::exceptionStr(e);
  }
}

TemporaryFile& TemporaryFile::getTempFile() {
  static TemporaryFile sTempFile(6 << 20); // 6MiB
  return sTempFile;
}
} // namespace test
} // namespace folly
