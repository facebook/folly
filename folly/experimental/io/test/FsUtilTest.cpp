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

#include <folly/experimental/io/FsUtil.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::fs;

namespace {
// We cannot use EXPECT_EQ(a, b) due to a bug in gtest 1.6.0: gtest wants
// to print path as a container even though it has operator<< defined,
// and as path is a container of path, this leads to infinite
// recursion.
void expectPathEq(const path& a, const path& b) {
  EXPECT_TRUE(a == b) << "expected path=" << a << "\nactual path=" << b;
}
}  // namespace

TEST(Simple, Path) {
  path root("/");
  path abs1("/hello/world");
  path rel1("meow");
  EXPECT_TRUE(starts_with(abs1, root));
  EXPECT_FALSE(starts_with(rel1, root));
  expectPathEq(path("hello/world"), remove_prefix(abs1, root));
  EXPECT_THROW({remove_prefix(rel1, root);}, filesystem_error);

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
  expectPathEq(path("world"), remove_prefix(abs1, abs2));
  expectPathEq(path("world"), remove_prefix(abs1, abs3));
  expectPathEq(path(), remove_prefix(abs1, abs4));
  EXPECT_THROW({remove_prefix(abs1, abs5);}, filesystem_error);
  EXPECT_THROW({remove_prefix(abs1, abs6);}, filesystem_error);
}

TEST(Simple, CanonicalizeParent) {
  path a("/usr/bin/tail");
  path b("/usr/lib/../bin/tail");
  path c("/usr/bin/DOES_NOT_EXIST_ASDF");
  path d("/usr/lib/../bin/DOES_NOT_EXIST_ASDF");

  expectPathEq(a, canonical(a));
  expectPathEq(a, canonical_parent(b));
  expectPathEq(a, canonical(b));
  expectPathEq(a, canonical_parent(b));
  EXPECT_THROW({canonical(c);}, filesystem_error);
  EXPECT_THROW({canonical(d);}, filesystem_error);
  expectPathEq(c, canonical_parent(c));
  expectPathEq(c, canonical_parent(d));
}
