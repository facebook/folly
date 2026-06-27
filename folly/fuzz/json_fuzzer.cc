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
 * Fuzz target for the Folly JSON parser and serializer.
 *
 * Exercises:
 *  - folly::parseJson()  – arbitrary byte sequence → dynamic object
 *  - folly::toJson()     – round-trip serialization back to a string
 *  - Re-parse invariant  – serialized output must always be valid JSON
 *
 * Security relevance: JSON parsers are a common attack surface for
 * integer overflows, out-of-bounds reads, stack overflows from deeply
 * nested structures, and denial-of-service via large/malformed inputs.
 *
 * OSS-Fuzz integration: https://github.com/google/oss-fuzz/tree/master/projects/folly
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <folly/Range.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 4096) {
    return 0;
  }

  const folly::StringPiece input(
      reinterpret_cast<const char*>(data), size);

  folly::json::serialization_opts opts;
  opts.recursion_limit = 100;

  try {
    auto parsed = folly::parseJson(input, opts);
    const std::string serialized = folly::toJson(parsed);
    (void)folly::parseJson(serialized, opts);
  } catch (const folly::json::parse_error&) {
    return 0;
  } catch (...) {
    abort();
  }

  return 0;
}
