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
namespace async_base_test_lib_detail {
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
  int rc = posix_memalign(&buf, kODirectAlign, size);
  CHECK_EQ(rc, 0) << folly::errnoStr(rc);
  return ManagedBuffer(reinterpret_cast<char*>(buf), free);
}

} // namespace async_base_test_lib_detail
} // namespace test
} // namespace folly
