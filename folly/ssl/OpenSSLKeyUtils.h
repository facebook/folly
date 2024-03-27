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

#pragma once

#include <folly/Range.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

namespace folly {
namespace ssl {
class PasswordCollector;

class OpenSSLKeyUtils {
 public:
  /**
   * Reads a private key from memory and returns it as a EVP_PKEY pointer.
   * @param range Buffer to parse.
   * @return A EVP_PKEY pointer.
   */
  static EvpPkeyUniquePtr readPrivateKeyFromBuffer(ByteRange range);

  /**
   * Reads a private key from memory and decrypts it with the given password
   * and returns a EVP_PKEY pointer.
   * @param range Buffer to parse.
   * @param password Password to use.
   * @return A EVP_PKEY pointer.
   */
  static EvpPkeyUniquePtr readPrivateKeyFromBuffer(
      ByteRange range, StringPiece password);

  /**
   * Reads a private key from memory and decrypts it with the given password
   * collector and returns a EVP_PKEY pointer.
   * @param range Buffer to parse.
   * @param pwCollector PasswordCollector to use.
   * @return A EVP_PKEY pointer.
   */
  static EvpPkeyUniquePtr readPrivateKeyFromBuffer(
      ByteRange range, const PasswordCollector* pwCollector);
};
} // namespace ssl
} // namespace folly
