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

#include <filesystem>
#include <folly/FileUtil.h>
#include <folly/json.h>
#include <folly/portability/GTest.h>
#include <folly/testing/TestUtil.h>

namespace folly::json::test {

void maskNan(folly::dynamic& a) {
  if (a.isDouble() && std::isnan(a.asDouble())) {
    a = "__folly_masked_nan";
  }
  if (a.isArray()) {
    for (folly::dynamic& i : a) {
      maskNan(i);
    }
  }
  if (a.isObject()) {
    for (auto& [key, val] : a.items()) {
      maskNan(val);
    }
  }
}

folly::dynamic parseJson5(std::string_view input) {
  return folly::parseJson(input, {.allow_json5_experimental = true});
}

void testJson5ParseRoundtrip(std::string_view input, bool passed) try {
  auto v1 = parseJson5(input);
  auto s1 = folly::json::serialize(v1, {.allow_nan_inf = true});
  auto v2 = parseJson5(s1);
  maskNan(v1), maskNan(v2);
  if (passed) {
    EXPECT_EQ(v1, v2);
  } else {
    EXPECT_NE(v1, v2);
  }
} catch (std::exception&) {
  EXPECT_FALSE(passed);
}

constexpr auto kTestRootPath = "folly/json/test/json5-tests";

constexpr auto kPassedTests = {
    // Arrays
    "src/arrays/empty-array.json",
    "src/arrays/leading-comma-array.js",
    "src/arrays/lone-trailing-comma-array.js",
    "src/arrays/no-comma-array.txt",
    "src/arrays/regular-array.json",
    "src/arrays/trailing-comma-array.json5",
    // Comments
    "src/comments/block-comment-following-array-element.json5",
    "src/comments/block-comment-following-top-level-value.json5",
    "src/comments/block-comment-in-string.json",
    "src/comments/block-comment-preceding-top-level-value.json5",
    "src/comments/block-comment-with-asterisks.json5",
    "src/comments/inline-comment-following-array-element.json5",
    "src/comments/inline-comment-following-top-level-value.json5",
    "src/comments/inline-comment-in-string.json",
    "src/comments/inline-comment-preceding-top-level-value.json5",
    "src/comments/top-level-block-comment.txt",
    "src/comments/top-level-inline-comment.txt",
    "src/comments/unterminated-block-comment.txt",
    // Misc
    "src/misc/empty.txt",
    "src/misc/npm-package.json",
    // New lines
    "src/new-lines/comment-cr.json5",
    "src/new-lines/comment-crlf.json5",
    "src/new-lines/comment-lf.json5",
    // Numbers
    "src/numbers/float.json",
    "src/numbers/float-leading-zero.json",
    "src/numbers/float-trailing-decimal-point.json5",
    "src/numbers/float-trailing-decimal-point-with-integer-exponent.json5",
    "src/numbers/float-with-integer-exponent.json",
    "src/numbers/hexadecimal-empty.txt",
    "src/numbers/integer.json",
    "src/numbers/integer-with-float-exponent.txt",
    "src/numbers/integer-with-hexadecimal-exponent.txt",
    "src/numbers/integer-with-integer-exponent.json",
    "src/numbers/integer-with-negative-float-exponent.txt",
    "src/numbers/integer-with-negative-hexadecimal-exponent.txt",
    "src/numbers/integer-with-negative-integer-exponent.json",
    "src/numbers/integer-with-negative-zero-integer-exponent.json",
    "src/numbers/integer-with-positive-float-exponent.txt",
    "src/numbers/integer-with-positive-hexadecimal-exponent.txt",
    "src/numbers/integer-with-positive-integer-exponent.json",
    "src/numbers/integer-with-positive-zero-integer-exponent.json",
    "src/numbers/integer-with-zero-integer-exponent.json",
    "src/numbers/lone-decimal-point.txt",
    "src/numbers/negative-float.json",
    "src/numbers/negative-float-leading-zero.json",
    "src/numbers/infinity.json5",
    "src/numbers/nan.json5",
    "src/numbers/negative-float-trailing-decimal-point.json5",
    "src/numbers/negative-infinity.json5",
    "src/numbers/negative-integer.json",
    "src/numbers/negative-zero-float.json",
    "src/numbers/negative-zero-float-trailing-decimal-point.json5",
    "src/numbers/negative-zero-integer.json",
    "src/numbers/positive-noctal.js",
    "src/numbers/positive-octal.txt",
    "src/numbers/positive-zero-octal.txt",
    "src/numbers/zero-float.json",
    "src/numbers/zero-float-trailing-decimal-point.json5",
    "src/numbers/zero-integer.json",
    "src/numbers/zero-integer-with-integer-exponent.json",
    // Objects
    "src/objects/duplicate-keys.json",
    "src/objects/empty-object.json",
    "src/objects/illegal-unquoted-key-number.txt",
    "src/objects/illegal-unquoted-key-symbol.txt",
    "src/objects/leading-comma-object.txt",
    "src/objects/lone-trailing-comma-object.txt",
    "src/objects/no-comma-object.txt",
    "src/objects/trailing-comma-object.json5",
};

constexpr auto kFailedTests = {
    // Misc
    "src/misc/npm-package.json5",
    "src/misc/readme-example.json5",
    "src/misc/valid-whitespace.json5",
    // New lines
    "src/new-lines/escaped-cr.json5",
    "src/new-lines/escaped-crlf.json5",
    "src/new-lines/escaped-lf.json5",
    // Numbers
    "src/numbers/float-leading-decimal-point.json5",
    "src/numbers/hexadecimal.json5",
    "src/numbers/hexadecimal-lowercase-letter.json5",
    "src/numbers/hexadecimal-uppercase-x.json5",
    "src/numbers/hexadecimal-with-integer-exponent.json5",
    "src/numbers/negative-float-leading-decimal-point.json5",
    "src/numbers/negative-hexadecimal.json5",
    "src/numbers/negative-noctal.js",
    "src/numbers/negative-octal.txt",
    "src/numbers/negative-zero-float-leading-decimal-point.json5",
    "src/numbers/negative-zero-hexadecimal.json5",
    "src/numbers/negative-zero-octal.txt",
    "src/numbers/noctal.js",
    "src/numbers/noctal-with-leading-octal-digit.js",
    "src/numbers/octal.txt",
    "src/numbers/positive-float.json5",
    "src/numbers/positive-float-leading-decimal-point.json5",
    "src/numbers/positive-float-leading-zero.json5",
    "src/numbers/positive-float-trailing-decimal-point.json5",
    "src/numbers/positive-hexadecimal.json5",
    "src/numbers/positive-infinity.json5",
    "src/numbers/positive-integer.json5",
    "src/numbers/positive-zero-float.json5",
    "src/numbers/positive-zero-float-leading-decimal-point.json5",
    "src/numbers/positive-zero-float-trailing-decimal-point.json5",
    "src/numbers/positive-zero-hexadecimal.json5",
    "src/numbers/positive-zero-integer.json5",
    "src/numbers/zero-float-leading-decimal-point.json5",
    "src/numbers/zero-hexadecimal.json5",
    "src/numbers/zero-octal.txt",
    // Objects
    "src/objects/reserved-unquoted-key.json5",
    "src/objects/single-quoted-key.json5",
    "src/objects/unquoted-keys.json5",
    // Strings
    "src/strings/escaped-single-quoted-string.json5",
    "src/strings/multi-line-string.json5",
    "src/strings/single-quoted-string.json5",
    "src/strings/unescaped-multi-line-string.txt",
};

std::string getTestResource(std::string testPath) {
  auto path = folly::test::find_resource(kTestRootPath) / testPath;
  std::string buffer;
  folly::readFile(path.c_str(), buffer);
  return buffer;
}

TEST(Json5Test, Parse) {
  for (const auto& p : kPassedTests) {
    auto ext = std::filesystem::path{p}.extension();
    SCOPED_TRACE(p);

    if (ext == ".txt" || ext == ".js") {
      // files with ".txt" and ".js" are not valid json.
      // Parser is expected to reject them.
      EXPECT_THROW(parseJson5(getTestResource(p)), std::exception);
    } else {
      EXPECT_TRUE(ext == ".json" || ext == ".json5");
      testJson5ParseRoundtrip(getTestResource(p), true);
    }
  }

  for (const auto& p : kFailedTests) {
    auto ext = std::filesystem::path{p}.extension();
    SCOPED_TRACE(p);

    if (ext == ".txt" || ext == ".js") {
      // Since this test is marked as failed, parser is expected to accept it.
      EXPECT_NO_THROW(parseJson5(getTestResource(p)));
    } else {
      EXPECT_TRUE(ext == ".json" || ext == ".json5");
      testJson5ParseRoundtrip(getTestResource(p), false);
    }
  }
}

} // namespace folly::json::test
