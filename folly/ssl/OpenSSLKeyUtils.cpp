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

#include <folly/ssl/OpenSSLKeyUtils.h>

#include <folly/portability/OpenSSL.h>
#include <folly/ssl/PasswordCollector.h>

namespace {
folly::ssl::BioUniquePtr toBio(folly::ByteRange range) {
  folly::ssl::BioUniquePtr bio(BIO_new_mem_buf(range.data(), range.size()));
  if (!bio) {
    throw std::runtime_error("Failed to create BIO");
  }
  return bio;
}

int passwordCallback(char* password, int size, int, void* data) {
  if (!password || !data || size < 1) {
    return 0;
  }
  std::string userPassword;
  static_cast<folly::ssl::PasswordCollector const*>(data)->getPassword(
      userPassword, size);
  if (userPassword.empty()) {
    return 0;
  }
  auto length = std::min(static_cast<int>(userPassword.size()), size - 1);
  memcpy(password, userPassword.data(), length);
  password[length] = '\0';
  return length;
}
} // namespace

namespace folly {
namespace ssl {
EvpPkeyUniquePtr OpenSSLKeyUtils::readPrivateKeyFromBuffer(ByteRange range) {
  auto bio = toBio(range);
  EvpPkeyUniquePtr pkey(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (!pkey) {
    throw std::runtime_error("Failed to read key");
  }
  return pkey;
}

EvpPkeyUniquePtr OpenSSLKeyUtils::readPrivateKeyFromBuffer(
    ByteRange range, StringPiece password) {
  auto bio = toBio(range);
  EvpPkeyUniquePtr pkey(PEM_read_bio_PrivateKey(
      bio.get(), nullptr, nullptr, (void*)password.data()));
  if (!pkey) {
    throw std::runtime_error("Failed to read key");
  }
  return pkey;
}

EvpPkeyUniquePtr OpenSSLKeyUtils::readPrivateKeyFromBuffer(
    ByteRange range, const PasswordCollector* pwCollector) {
  auto bio = toBio(range);
  EvpPkeyUniquePtr pkey(PEM_read_bio_PrivateKey(
      bio.get(), nullptr, passwordCallback, (void*)pwCollector));
  if (!pkey) {
    throw std::runtime_error("Failed to read key");
  }
  return pkey;
}
} // namespace ssl
} // namespace folly
