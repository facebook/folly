/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/experimental/io/Stream.h"

#include <vector>
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "folly/Benchmark.h"
#include "folly/experimental/TestUtil.h"

using namespace folly;

namespace {

std::vector<std::string> streamSplit(const std::string& str, char delimiter,
                                     size_t maxChunkSize = (size_t)-1) {
  size_t pos = 0;
  auto cb = [&] (ByteRange& sp) mutable -> bool {
    if (pos == str.size()) return false;
    size_t n = std::min(str.size() - pos, maxChunkSize);
    sp.reset(reinterpret_cast<const unsigned char*>(&(str[pos])), n);
    pos += n;
    return true;
  };

  std::vector<std::string> result;
  for (auto line : makeInputByteStreamSplitter(delimiter, cb)) {
    result.push_back(StringPiece(line).str());
  }

  return result;
}

}  // namespace

TEST(InputByteStreamSplitter, Empty) {
  {
    auto pieces = streamSplit("", ',');
    EXPECT_EQ(0, pieces.size());
  }

  // The last delimiter is eaten, just like std::getline
  {
    auto pieces = streamSplit(",", ',');
    EXPECT_EQ(1, pieces.size());
    EXPECT_EQ("", pieces[0]);
  }

  {
    auto pieces = streamSplit(",,", ',');
    EXPECT_EQ(2, pieces.size());
    EXPECT_EQ("", pieces[0]);
    EXPECT_EQ("", pieces[1]);
  }
}

TEST(InputByteStreamSplitter, Simple) {
  std::string str = "hello,, world, goodbye, meow";

  for (size_t chunkSize = 1; chunkSize <= str.size(); ++chunkSize) {
    auto pieces = streamSplit(str, ',', chunkSize);
    EXPECT_EQ(5, pieces.size());
    EXPECT_EQ("hello", pieces[0]);
    EXPECT_EQ("", pieces[1]);
    EXPECT_EQ(" world", pieces[2]);
    EXPECT_EQ(" goodbye", pieces[3]);
    EXPECT_EQ(" meow", pieces[4]);
  }
}

TEST(ByLine, Simple) {
  test::TemporaryFile file("ByLine");
  static const std::string lines(
      "Hello world\n"
      "This is the second line\n"
      "\n"
      "\n"
      "a few empty lines above\n"
      "incomplete last line");
  EXPECT_EQ(lines.size(), write(file.fd(), lines.data(), lines.size()));

  auto expected = streamSplit(lines, '\n');
  std::vector<std::string> found;
  for (auto& line : byLine(file.path())) {
    found.push_back(StringPiece(line).str());
  }

  EXPECT_TRUE(expected == found);
}

int main(int argc, char *argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  auto ret = RUN_ALL_TESTS();
  if (!ret) {
    folly::runBenchmarksOnFlag();
  }
  return ret;
}

