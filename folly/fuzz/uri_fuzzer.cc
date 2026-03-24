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
 * Fuzz target for folly::Uri.
 *
 * Exercises:
 *  - folly::Uri::tryFromString() – non-throwing parse of arbitrary bytes
 *  - All field accessors: scheme, username, password, host, hostname,
 *    port, path, query, fragment, authority
 *  - str() serialization
 *  - getQueryParams() – key/value decomposition of the query string
 *
 * Security relevance: URI parsers are frequently exploited for
 * path-traversal, SSRF, credential leakage via crafted authority
 * components, and integer overflow in port-number parsing.
 *
 * OSS-Fuzz integration: https://github.com/google/oss-fuzz/tree/master/projects/folly
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <folly/Range.h>
#include <folly/Uri.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 4096) {
    return 0;
  }

  const folly::StringPiece input(
      reinterpret_cast<const char*>(data), size);

  auto result = folly::Uri::tryFromString(input);
  if (!result.hasValue()) {
    return 0;
  }

  try {
    auto& uri = result.value();
    (void)uri.scheme();
    (void)uri.username();
    (void)uri.password();
    (void)uri.host();
    (void)uri.hostname();
    (void)uri.port();
    (void)uri.path();
    (void)uri.query();
    (void)uri.fragment();
    (void)uri.authority();
    (void)uri.str();
    (void)uri.getQueryParams();
  } catch (...) {
    abort();
  }

  return 0;
}
