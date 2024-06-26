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

#include <folly/experimental/io/FsUtil.h>

#include <array>
#include <fstream>
#include <random>

#include <fmt/core.h>
#include <glog/logging.h>

#include <folly/String.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace folly::fs;

namespace {

class TempDir {
 public:
  static fs::path make_path() {
    std::random_device rng;
    std::uniform_int_distribution<uint64_t> dist;
    auto basename = "folly-unit-test-" + fmt::format("{:016x}", dist(rng));
    return fs::canonical(fs::temp_directory_path()) / basename;
  }
  fs::path const path{make_path()};
  TempDir() { fs::create_directory(path); }
  ~TempDir() { fs::remove_all(path); }
};

} // namespace

TEST(Simple, Path) {
  path root("/");
  path abs1("/hello/world");
  path rel1("meow");
  EXPECT_TRUE(starts_with(abs1, root));
  EXPECT_FALSE(starts_with(rel1, root));
  EXPECT_EQ(path("hello/world"), remove_prefix(abs1, root));
  EXPECT_THROW({ remove_prefix(rel1, root); }, filesystem_error);

  path abs2("/hello");
  path abs3("/hello/");
  path abs4("/hello/world");
  path abs5("/hello/world/");
  path abs6("/hello/wo");
  EXPECT_TRUE(starts_with(abs1, abs2));
  EXPECT_TRUE(starts_with(abs1, abs3));
  EXPECT_TRUE(starts_with(abs1, abs4));
  EXPECT_FALSE(starts_with(abs1, abs5));
  EXPECT_FALSE(starts_with(abs1, abs6));
  EXPECT_EQ(path("world"), remove_prefix(abs1, abs2));
  EXPECT_EQ(path("world"), remove_prefix(abs1, abs3));
  EXPECT_EQ(path(), remove_prefix(abs1, abs4));
  EXPECT_THROW({ remove_prefix(abs1, abs5); }, filesystem_error);
  EXPECT_THROW({ remove_prefix(abs1, abs6); }, filesystem_error);
}

TEST(Simple, CanonicalizeParent) {
  TempDir tmpd;

  path a = tmpd.path / "usr/bin/tail";
  path b = tmpd.path / "usr/lib/../bin/tail";
  path c = tmpd.path / "usr/bin/DOES_NOT_EXIST_ASDF";
  path d = tmpd.path / "usr/lib/../bin/DOES_NOT_EXIST_ASDF";

  create_directories(a.parent_path());
  create_directories(a.parent_path() / "../lib");
  std::ofstream(a.native()) << "foobar" << std::endl;

  EXPECT_EQ(a, canonical(a));
  EXPECT_EQ(a, canonical_parent(b));
  EXPECT_EQ(a, canonical(b));
  EXPECT_EQ(a, canonical_parent(b));
  EXPECT_THROW({ canonical(c); }, filesystem_error);
  EXPECT_THROW({ canonical(d); }, filesystem_error);
  EXPECT_EQ(c, canonical_parent(c));
  EXPECT_EQ(c, canonical_parent(d));
}

TEST(Simple, UniquePath) {
  constexpr auto size = size_t(1) << 10;
  constexpr auto tags =
      std::array{"foo", "bar", "baz", "cat", "dog", "bat", "rat", "tar", "bar"};
  auto const model = join("-%%-", tags);
  auto const match = testing::MatchesRegex(join("-[0-9a-f]{2}-", tags));
  std::unordered_set<std::string> paths;
  for (size_t i = 0; i < size; ++i) {
    auto res = std_fs_unique_path(std_fs::path(model)).string();
    EXPECT_THAT(res, match);
    paths.insert(std::move(res));
  }
  EXPECT_EQ(size, paths.size());
}

TEST(Simple, UniquePathDefaultModel) {
  constexpr auto size = size_t(1) << 10;
  auto const match = testing::MatchesRegex("([0-9a-f]{4}-){3}[0-9a-f]{4}");
  std::unordered_set<std::string> paths;
  for (size_t i = 0; i < size; ++i) {
    auto res = std_fs_unique_path().string();
    EXPECT_THAT(res, match);
    paths.insert(std::move(res));
  }
  EXPECT_EQ(size, paths.size());
}
