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

#include <folly/detail/FileUtilDetail.h>
#include <folly/portability/GTest.h>

namespace folly {
namespace fileutil_detail {

class FileUtilDetailTest : public ::testing::Test {};

TEST_F(FileUtilDetailTest, TemporaryPathWithoutTemporaryDirectory) {
  auto path = std::string{"/a/b/c"};
  auto tempPath = getTemporaryFilePathString(path, std::string{});
  EXPECT_EQ(tempPath, "/a/b/c.XXXXXX");
}

TEST_F(FileUtilDetailTest, TemporaryPathWithoutTemporaryDirectoryRelative) {
  auto path = std::string{"a/b/c"};
  auto tempPath = getTemporaryFilePathString(path, std::string{});
  EXPECT_EQ(tempPath, "a/b/c.XXXXXX");
}

TEST_F(
    FileUtilDetailTest,
    TemporaryPathWithoutTemporaryDirectoryWithTempDirectoryAbsolute) {
  auto path = std::string{"a/b/c"};
  auto tempDirectory = std::string{"/temp/directory/"};
  auto tempPath = getTemporaryFilePathString(path, tempDirectory);
  EXPECT_EQ(tempPath, "/temp/directory/tempForAtomicWrite.XXXXXX");
}

TEST_F(
    FileUtilDetailTest,
    TemporaryPathWithoutTemporaryDirectoryWithTempDirectoryRelative) {
  auto path = std::string{"a/b/c"};
  auto tempDirectory = std::string{"temp/directory/"};
  auto tempPath = getTemporaryFilePathString(path, tempDirectory);
  EXPECT_EQ(tempPath, "temp/directory/tempForAtomicWrite.XXXXXX");
}

} // namespace fileutil_detail
} // namespace folly
