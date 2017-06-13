/*
 * Copyright 2004-present Facebook, Inc.
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
#include <folly/experimental/logging/Logger.h>
#include <folly/experimental/logging/LoggerDB.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(LoggerDB, lookupNameCanonicalization) {
  LoggerDB db{LoggerDB::TESTING};
  Logger foo{&db, "foo"};
  Logger foo2{&db, "..foo.."};
  EXPECT_EQ(foo.getCategory(), foo2.getCategory());

  Logger fooBar{&db, "foo.bar"};
  Logger fooBar2{&db, ".foo..bar"};
  EXPECT_EQ(fooBar.getCategory(), fooBar2.getCategory());
}

TEST(LoggerDB, getCategory) {
  LoggerDB db{LoggerDB::TESTING};
}

TEST(LoggerDB, processConfigString) {
  LoggerDB db{LoggerDB::TESTING};
  db.processConfigString("foo.bar=dbg5");
  EXPECT_EQ(LogLevel::DBG5, db.getCategory("foo.bar")->getLevel());
  EXPECT_EQ(LogLevel::DBG5, db.getCategory("foo.bar")->getEffectiveLevel());
  EXPECT_EQ(LogLevel::MAX_LEVEL, db.getCategory("foo")->getLevel());
  EXPECT_EQ(LogLevel::ERROR, db.getCategory("foo")->getEffectiveLevel());
  EXPECT_EQ(LogLevel::ERROR, db.getCategory("")->getLevel());
  EXPECT_EQ(LogLevel::ERROR, db.getCategory("")->getEffectiveLevel());

  EXPECT_EQ(LogLevel::MAX_LEVEL, db.getCategory("foo.bar.test")->getLevel());
  EXPECT_EQ(
      LogLevel::DBG5, db.getCategory("foo.bar.test")->getEffectiveLevel());

  db.processConfigString("sys=warn,foo.test=debug,foo.test.stuff=warn");
  EXPECT_EQ(LogLevel::WARN, db.getCategory("sys")->getLevel());
  EXPECT_EQ(LogLevel::WARN, db.getCategory("sys")->getEffectiveLevel());
  EXPECT_EQ(LogLevel::DEBUG, db.getCategory("foo.test")->getLevel());
  EXPECT_EQ(LogLevel::DEBUG, db.getCategory("foo.test")->getEffectiveLevel());
  EXPECT_EQ(LogLevel::WARN, db.getCategory("foo.test.stuff")->getLevel());
  EXPECT_EQ(
      LogLevel::DEBUG, db.getCategory("foo.test.stuff")->getEffectiveLevel());
}
