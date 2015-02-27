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
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <folly/File.h>
#include <folly/Range.h>
#include <folly/experimental/TestUtil.h>
#include <folly/gen/Base.h>
#include <folly/gen/File.h>

using namespace folly::gen;
using namespace folly;
using std::string;
using std::vector;

TEST(FileGen, ByLine) {
  auto collect = eachTo<std::string>() | as<vector>();
  test::TemporaryFile file("ByLine");
  static const std::string lines(
      "Hello world\n"
      "This is the second line\n"
      "\n"
      "\n"
      "a few empty lines above\n"
      "incomplete last line");
  EXPECT_EQ(lines.size(), write(file.fd(), lines.data(), lines.size()));

  auto expected = from({lines}) | resplit('\n') | collect;
  auto found = byLine(file.path().c_str()) | collect;

  EXPECT_TRUE(expected == found);
}

class FileGenBufferedTest : public ::testing::TestWithParam<int> { };

TEST_P(FileGenBufferedTest, FileWriter) {
  size_t bufferSize = GetParam();
  test::TemporaryFile file("FileWriter");

  static const std::string lines(
      "Hello world\n"
      "This is the second line\n"
      "\n"
      "\n"
      "a few empty lines above\n");

  auto src = from({lines, lines, lines, lines, lines, lines, lines, lines});
  auto collect = eachTo<std::string>() | as<vector>();
  auto expected = src | resplit('\n') | collect;

  src | eachAs<StringPiece>() | toFile(File(file.fd()), bufferSize);
  auto found = byLine(file.path().c_str()) | collect;

  EXPECT_TRUE(expected == found);
}

INSTANTIATE_TEST_CASE_P(
    DifferentBufferSizes,
    FileGenBufferedTest,
    ::testing::Values(0, 1, 2, 4, 8, 64, 4096));
int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
