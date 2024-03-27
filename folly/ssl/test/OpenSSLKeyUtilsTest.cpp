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

#include <folly/portability/GTest.h>
#include <folly/ssl/PasswordCollector.h>

using namespace folly;
using namespace folly::ssl;
using namespace std::literals;
using namespace testing;

// @lint-ignore-every PRIVATEKEY

constexpr std::string_view kTestKey =
    R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgzomq58CaWH7452B6
p2yqTBWkSWASUI/4x/7aNyovgEuhRANCAAS0eJVMaB9eeZ5BkNHW5+2lp/IX98+z
Sd6bGGI/5naqm2GQvkwTknZMh6ViP3BHI3PO8mrAil9bj7FrVroPO7w9
-----END PRIVATE KEY-----
)"sv;

constexpr std::string_view kEncryptedTestKey =
    R"(-----BEGIN ENCRYPTED PRIVATE KEY-----
MIHsMFcGCSqGSIb3DQEFDTBKMCkGCSqGSIb3DQEFDDAcBAh+L4morlTFPQICCAAw
DAYIKoZIhvcNAgkFADAdBglghkgBZQMEAQIEEIKOgD0bmxgz0TqXxEqaWioEgZBE
4ayEn9rkVIgeaw+J3rXRpDMbS0AQ0iS6wTpSLEshawfh2bwVYdnFxObdHAAuIjrj
stA3KIPu+AiuUsQgeRidf9HEI3zyIccwwMRjQ/NiGjqXxjMoTEXGezLuvJRmqu56
dz4QCr6hc3RydcFlRXZtompluo03RlaImN5HNYcZyeLz5gxuAcvi4Dw8v+UlZiQ=
-----END ENCRYPTED PRIVATE KEY-----
)"sv;

constexpr std::string_view kTestKeyPassword = "test"sv;

namespace {
std::string encodePrivateKey(const ssl::EvpPkeyUniquePtr& pkey) {
  ssl::BioUniquePtr bio(BIO_new(BIO_s_mem()));
  if (!PEM_write_bio_PrivateKey(
          bio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr)) {
    throw std::runtime_error("Failed to encode privatekey");
  }
  BUF_MEM* bptr = nullptr;
  BIO_get_mem_ptr(bio.get(), &bptr);
  return {bptr->data, bptr->length};
}

class TestPasswordCollector : public PasswordCollector {
 public:
  explicit TestPasswordCollector(std::string_view password)
      : password_{password} {}
  void getPassword(std::string& password, int /* size */) const override {
    password = password_;
  }
  const std::string& describe() const override { return description_; }

 private:
  std::string password_;
  std::string description_;
};
} // namespace

TEST(OpenSSLKeyUtils, TestReadPrivateKeyFromBuffer) {
  {
    SCOPED_TRACE("Read key without password");
    auto key = OpenSSLKeyUtils::readPrivateKeyFromBuffer(ByteRange(kTestKey));
    EXPECT_EQ(kTestKey, encodePrivateKey(key));
  }

  {
    SCOPED_TRACE("Read key with empty password");
    auto key =
        OpenSSLKeyUtils::readPrivateKeyFromBuffer(ByteRange(kTestKey), "");
    EXPECT_EQ(kTestKey, encodePrivateKey(key));
  }

  {
    SCOPED_TRACE("Read key with NULL password collector");
    auto key =
        OpenSSLKeyUtils::readPrivateKeyFromBuffer(ByteRange(kTestKey), nullptr);
    EXPECT_EQ(kTestKey, encodePrivateKey(key));
  }

  {
    SCOPED_TRACE(
        "Read key witht empty password returned from password collector");
    TestPasswordCollector pwCollector({});
    auto key = OpenSSLKeyUtils::readPrivateKeyFromBuffer(
        ByteRange(kTestKey), &pwCollector);
    EXPECT_EQ(kTestKey, encodePrivateKey(key));
  }

  {
    SCOPED_TRACE("Read encrypted key with password");
    auto key = OpenSSLKeyUtils::readPrivateKeyFromBuffer(
        ByteRange(kEncryptedTestKey), kTestKeyPassword);
    EXPECT_EQ(kTestKey, encodePrivateKey(key));
  }

  {
    SCOPED_TRACE("Read encrypted key with password collector");
    TestPasswordCollector pwCollector(kTestKeyPassword);
    auto key = OpenSSLKeyUtils::readPrivateKeyFromBuffer(
        ByteRange(kEncryptedTestKey), &pwCollector);
    EXPECT_EQ(kTestKey, encodePrivateKey(key));
  }

  {
    SCOPED_TRACE("Throw when fail to read bad key");
    std::string badKey = "bad key";
    EXPECT_THROW(
        OpenSSLKeyUtils::readPrivateKeyFromBuffer(ByteRange(badKey)),
        std::runtime_error);
  }

  {
    SCOPED_TRACE("Throw when fail to decrypt the key with wrong password");
    EXPECT_THROW(
        OpenSSLKeyUtils::readPrivateKeyFromBuffer(
            ByteRange(kEncryptedTestKey), "hello"),
        std::runtime_error);
  }

  {
    SCOPED_TRACE(
        "Throw when fail to decrypt the key with wrong password collector");
    TestPasswordCollector pwCollector("hello");
    EXPECT_THROW(
        OpenSSLKeyUtils::readPrivateKeyFromBuffer(
            ByteRange(kEncryptedTestKey), &pwCollector),
        std::runtime_error);
  }
}
