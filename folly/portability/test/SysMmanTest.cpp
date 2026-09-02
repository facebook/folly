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

#include <folly/portability/SysMman.h>

#include <cerrno>
#include <cstring>

#include <gtest/gtest.h>

using namespace ::testing;

namespace {
constexpr size_t kLength = 4096;
} // namespace

TEST(SysMmanTest, AnonymousPrivateMapRoundTrips) {
  auto* p = mmap(
      nullptr,
      kLength,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1,
      0);
  ASSERT_NE(MAP_FAILED, p);
  memset(p, 0x5A, kLength);
  EXPECT_EQ(0, munmap(p, kLength));
}

#ifdef _WIN32

// These map-argument combinations are rejected only by the Windows shim; POSIX
// either accepts them or reports a platform-dependent errno.

TEST(SysMmanTest, AnonymousSharedIsRejectedWithErrno) {
  errno = 0;
  auto* p = mmap(
      nullptr,
      kLength,
      PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_SHARED,
      -1,
      0);
  EXPECT_EQ(MAP_FAILED, p);
  EXPECT_EQ(EINVAL, errno);
}

TEST(SysMmanTest, FileBackedWithoutDescriptorIsRejectedWithErrno) {
  errno = 0;
  auto* p = mmap(nullptr, kLength, PROT_READ, MAP_SHARED, -1, 0);
  EXPECT_EQ(MAP_FAILED, p);
  EXPECT_EQ(EBADF, errno);
}

TEST(SysMmanTest, UnsupportedProtectionIsRejectedWithErrno) {
  errno = 0;
  auto* p =
      mmap(nullptr, kLength, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  EXPECT_EQ(MAP_FAILED, p);
  EXPECT_EQ(EINVAL, errno);
}

#endif
