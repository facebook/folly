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

/**
 * Fuzz target for folly::tryTo<T>() string-to-scalar conversions (Conv.h).
 *
 * Exercises:
 *  - tryTo<T>() for all signed/unsigned integer types and bool
 *  - tryTo<float>() and tryTo<double>()
 *
 * All conversions use the non-throwing tryTo<> variant to avoid false-positive
 * "crashes" on expected parse failures.
 *
 * Security relevance: Integer and floating-point parsers are prone to
 * off-by-one errors, sign-extension bugs, and silent overflow/underflow.
 * UBSan is particularly effective at detecting these classes of bugs.
 *
 * OSS-Fuzz integration: https://github.com/google/oss-fuzz/tree/master/projects/folly
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include <folly/Conv.h>
#include <folly/Range.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 4096) {
    return 0;
  }

  const folly::StringPiece sp(
      reinterpret_cast<const char*>(data), size);

  // Test the raw input against every scalar type.
  (void)folly::tryTo<int8_t>(sp);
  (void)folly::tryTo<int16_t>(sp);
  (void)folly::tryTo<int32_t>(sp);
  (void)folly::tryTo<int64_t>(sp);
  (void)folly::tryTo<uint8_t>(sp);
  (void)folly::tryTo<uint16_t>(sp);
  (void)folly::tryTo<uint32_t>(sp);
  (void)folly::tryTo<uint64_t>(sp);
  (void)folly::tryTo<float>(sp);
  (void)folly::tryTo<double>(sp);
  (void)folly::tryTo<bool>(sp);

  // Negate the input to exercise negative-number and sign-handling paths.
  if (size > 0) {
    const std::string negated =
        "-" + std::string(reinterpret_cast<const char*>(data), size);
    const folly::StringPiece neg(negated);
    (void)folly::tryTo<int64_t>(neg);
    (void)folly::tryTo<double>(neg);
  }

  // Prepend "0x" to hit hexadecimal parsing branches.
  if (size > 0) {
    const std::string hexed =
        "0x" + std::string(reinterpret_cast<const char*>(data), size);
    const folly::StringPiece hex(hexed);
    (void)folly::tryTo<uint64_t>(hex);
    (void)folly::tryTo<int64_t>(hex);
  }

  return 0;
}
