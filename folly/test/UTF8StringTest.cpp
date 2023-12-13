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

#include <folly/UTF8String.h>

#include <folly/Range.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

using namespace folly;

const folly::StringPiece kTestUTF8 =
    reinterpret_cast<const char*>(u8"This is \U0001F602 stuff!");

TEST(UTF8StringPiece, validUtf8) {
  folly::StringPiece sp = kTestUTF8;
  UTF8StringPiece utf8 = sp;
  // utf8.size() not available since it's not a random-access range
  EXPECT_EQ(16, utf8.walk_size());
}

TEST(UTF8StringPiece, validSuffix) {
  UTF8StringPiece utf8 = kTestUTF8.subpiece(8);
  EXPECT_EQ(8, utf8.walk_size());
}

TEST(UTF8StringPiece, emptyMidCodepoint) {
  UTF8StringPiece utf8 = kTestUTF8.subpiece(9, 0); // okay since it's empty
  EXPECT_EQ(0, utf8.walk_size());
}

TEST(UTF8StringPiece, invalidMidCodepoint) {
  EXPECT_THROW(UTF8StringPiece(kTestUTF8.subpiece(9, 1)), std::out_of_range);
}

TEST(UTF8StringPiece, validImplicitConversion) {
  std::string input =
      reinterpret_cast<const char*>(u8"\U0001F602\U0001F602\U0001F602");
  auto checkImplicitCtor = [](UTF8StringPiece implicitCtor) {
    return implicitCtor.walk_size();
  };
  EXPECT_EQ(3, checkImplicitCtor(input));
}
