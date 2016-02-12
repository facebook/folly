/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/Demangle.h>

#include <gtest/gtest.h>

using folly::demangle;

namespace folly_test {
struct ThisIsAVeryLongStructureName {
};
}  // namespace folly_test

#if FOLLY_HAVE_CPLUS_DEMANGLE_V3_CALLBACK
TEST(Demangle, demangle) {
  char expected[] = "folly_test::ThisIsAVeryLongStructureName";
  EXPECT_STREQ(
      expected,
      demangle(typeid(folly_test::ThisIsAVeryLongStructureName)).c_str());

  {
    char buf[sizeof(expected)];
    EXPECT_EQ(sizeof(expected) - 1,
              demangle(typeid(folly_test::ThisIsAVeryLongStructureName),
                       buf, sizeof(buf)));
    EXPECT_STREQ(expected, buf);

    EXPECT_EQ(sizeof(expected) - 1,
              demangle(typeid(folly_test::ThisIsAVeryLongStructureName),
                       buf, 11));
    EXPECT_STREQ("folly_test", buf);
  }
}
#endif

TEST(Demangle, strlcpy) {
  char buf[6];

  EXPECT_EQ(3, folly::strlcpy(buf, "abc", 6));
  EXPECT_EQ('\0', buf[3]);
  EXPECT_EQ("abc", std::string(buf));

  EXPECT_EQ(7, folly::strlcpy(buf, "abcdefg", 3));
  EXPECT_EQ('\0', buf[2]);
  EXPECT_EQ("ab", std::string(buf));

  const char* big_string = "abcdefghijklmnop";

  EXPECT_EQ(strlen(big_string), folly::strlcpy(buf, big_string, sizeof(buf)));
  EXPECT_EQ('\0', buf[5]);
  EXPECT_EQ("abcde", std::string(buf));

  buf[0] = 'z';
  EXPECT_EQ(strlen(big_string), folly::strlcpy(buf, big_string, 0));
  EXPECT_EQ('z', buf[0]);  // unchanged, size = 0
}
