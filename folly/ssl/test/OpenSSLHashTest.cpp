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

#include <folly/ssl/OpenSSLHash.h>

#include <folly/io/IOBufQueue.h>
#include <folly/portability/GTest.h>

using namespace std;
using namespace folly;
using namespace folly::ssl;

namespace {

class OpenSSLHashTest : public testing::Test {};
} // namespace

TEST_F(OpenSSLHashTest, sha256) {
  IOBuf buf;
  buf.prependChain(IOBuf::wrapBuffer(ByteRange(StringPiece("foo"))));
  buf.prependChain(IOBuf::wrapBuffer(ByteRange(StringPiece("bar"))));
  EXPECT_EQ(3, buf.countChainElements());
  EXPECT_EQ(6, buf.computeChainDataLength());

  auto expected = vector<uint8_t>(32);
  auto combined = ByteRange(StringPiece("foobar"));
  SHA256(combined.data(), combined.size(), expected.data());

  auto out = vector<uint8_t>(32);
  OpenSSLHash::sha256(range(out), buf);
  EXPECT_EQ(expected, out);
}

TEST_F(OpenSSLHashTest, sha256_hashcopy) {
  std::array<uint8_t, 32> expected, actual1, actual2;
  constexpr StringPiece data{"foobar"};

  OpenSSLHash::hash(range(expected), EVP_sha256(), data);

  OpenSSLHash::Digest digest;
  digest.hash_init(EVP_sha256());
  digest.hash_update(ByteRange(data));

  OpenSSLHash::Digest copy1(digest); // copy constructor
  OpenSSLHash::Digest copy2 = digest; // copy assignment operator

  copy1.hash_final(range(actual1));
  copy2.hash_final(range(actual2));

  EXPECT_EQ(expected, actual1);
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, sha256_hashcopy_self) {
  std::array<uint8_t, 32> expected, actual;
  constexpr StringPiece data{"foobar"};

  OpenSSLHash::hash(range(expected), EVP_sha256(), data);

  OpenSSLHash::Digest digest;
  digest.hash_init(EVP_sha256());
  digest.hash_update(ByteRange(data));

  OpenSSLHash::Digest* ptr = &digest;
  digest = *ptr; // test copy of an object to itself

  digest.hash_final(range(actual));

  EXPECT_EQ(expected, actual);
}

TEST_F(OpenSSLHashTest, sha256_hashcopy_from_default_constructed) {
  std::array<uint8_t, 32> expected, actual1, actual2;
  constexpr StringPiece data{"foobar"};

  OpenSSLHash::hash(range(expected), EVP_sha256(), data);

  OpenSSLHash::Digest digest;
  OpenSSLHash::Digest copy1(digest); // copy constructor
  OpenSSLHash::Digest copy2 = digest; // copy assignment operator

  copy1.hash_init(EVP_sha256());
  copy1.hash_update(ByteRange(data));
  copy1.hash_final(range(actual1));
  EXPECT_EQ(expected, actual1);

  copy2.hash_init(EVP_sha256());
  copy2.hash_update(ByteRange(data));
  copy2.hash_final(range(actual2));
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, sha256_hashmove) {
  std::array<uint8_t, 32> expected, actual1, actual2;
  constexpr StringPiece data{"foobar"};

  OpenSSLHash::hash(range(expected), EVP_sha256(), data);

  OpenSSLHash::Digest digest;
  digest.hash_init(EVP_sha256());
  digest.hash_update(ByteRange(data));
  OpenSSLHash::Digest copy1(std::move(digest)); // move constructor
  copy1.hash_final(range(actual1));
  EXPECT_EQ(expected, actual1);

  digest = OpenSSLHash::Digest{}; // should be safe to reassign to moved object
  digest.hash_init(EVP_sha256());
  digest.hash_update(ByteRange(data));
  OpenSSLHash::Digest copy2 = std::move(digest); // move assignment operator
  copy2.hash_final(range(actual2));
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, sha256_hashmove_self) {
  std::array<uint8_t, 32> expected, actual;
  constexpr StringPiece data{"foobar"};

  OpenSSLHash::hash(range(expected), EVP_sha256(), data);

  OpenSSLHash::Digest digest;
  digest.hash_init(EVP_sha256());
  digest.hash_update(ByteRange(data));

  OpenSSLHash::Digest* ptr = &digest;
  digest = std::move(*ptr); // test move of an object to itself

  digest.hash_final(range(actual));

  EXPECT_EQ(expected, actual);
}

TEST_F(OpenSSLHashTest, sha256_hashmove_from_default_constructed) {
  std::array<uint8_t, 32> expected, actual1, actual2;
  constexpr StringPiece data{"foobar"};

  OpenSSLHash::hash(range(expected), EVP_sha256(), data);

  OpenSSLHash::Digest digest1;
  OpenSSLHash::Digest copy1(std::move(digest1)); // move constructor
  OpenSSLHash::Digest digest2;
  OpenSSLHash::Digest copy2 = std::move(digest2); // move assignment operator

  copy1.hash_init(EVP_sha256());
  copy1.hash_update(ByteRange(data));
  copy1.hash_final(range(actual1));
  EXPECT_EQ(expected, actual1);

  copy2.hash_init(EVP_sha256());
  copy2.hash_update(ByteRange(data));
  copy2.hash_final(range(actual2));
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, sha256_hashcopy_intermediate) {
  std::array<uint8_t, 32> expected, actual1, actual2;
  constexpr StringPiece data1("foo");
  constexpr StringPiece data2("bar");

  OpenSSLHash::Digest digest;
  digest.hash_init(EVP_sha256());
  digest.hash_update(ByteRange(data1));

  OpenSSLHash::Digest copy1(digest); // copy constructor
  OpenSSLHash::Digest copy2 = digest; // copy assignment operator

  digest.hash_update(ByteRange(data2));
  digest.hash_final(range(expected));

  copy1.hash_update(ByteRange(data2));
  copy1.hash_final(range(actual1));
  EXPECT_EQ(expected, actual1);

  copy2.hash_update(ByteRange(data2));
  copy2.hash_final(range(actual2));
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, sha256_hashmove_intermediate) {
  std::array<uint8_t, 32> expected, actual1, actual2;
  constexpr StringPiece fulldata("foobar");
  constexpr StringPiece data1("foo");
  constexpr StringPiece data2("bar");

  OpenSSLHash::hash(range(expected), EVP_sha256(), fulldata);

  OpenSSLHash::Digest digest;
  digest.hash_init(EVP_sha256());
  digest.hash_update(ByteRange(data1));
  OpenSSLHash::Digest copy1(std::move(digest)); // move constructor
  copy1.hash_update(ByteRange(data2));
  copy1.hash_final(range(actual1));
  EXPECT_EQ(expected, actual1);

  digest.hash_init(EVP_sha256()); // should be safe to re-init moved object
  digest.hash_update(ByteRange(data1));
  OpenSSLHash::Digest copy2 = std::move(digest); // move assignment operator
  copy2.hash_update(ByteRange(data2));
  copy2.hash_final(range(actual2));
  EXPECT_EQ(expected, actual2);

  // Make sure it's safe to re-init moved object after move operator=()
  digest.hash_init(EVP_sha256());
}

TEST_F(OpenSSLHashTest, digest_update_without_init_throws) {
  OpenSSLHash::Digest digest;
  EXPECT_THROW(digest.hash_update(ByteRange{}), std::runtime_error);
}

TEST_F(OpenSSLHashTest, digest_final_without_init_throws) {
  OpenSSLHash::Digest digest;
  std::array<uint8_t, 32> out;
  EXPECT_THROW(digest.hash_final(range(out)), std::runtime_error);
}

TEST_F(OpenSSLHashTest, hmac_sha256) {
  auto key = ByteRange(StringPiece("qwerty"));

  IOBuf buf;
  buf.prependChain(IOBuf::wrapBuffer(ByteRange(StringPiece("foo"))));
  buf.prependChain(IOBuf::wrapBuffer(ByteRange(StringPiece("bar"))));
  EXPECT_EQ(3, buf.countChainElements());
  EXPECT_EQ(6, buf.computeChainDataLength());

  auto expected = vector<uint8_t>(32);
  auto combined = ByteRange(StringPiece("foobar"));
  HMAC(
      EVP_sha256(),
      key.data(),
      int(key.size()),
      combined.data(),
      combined.size(),
      expected.data(),
      nullptr);

  auto out = vector<uint8_t>(32);
  OpenSSLHash::hmac_sha256(range(out), key, buf);
  EXPECT_EQ(expected, out);
}

TEST_F(OpenSSLHashTest, hmac_sha256_hashcopy) {
  std::array<uint8_t, 32> expected, actual1, actual2;

  constexpr StringPiece data{"foobar"};
  constexpr StringPiece key{"qwerty"};

  OpenSSLHash::Hmac hmac;
  hmac.hash_init(EVP_sha256(), {key});
  hmac.hash_update({data});

  OpenSSLHash::Hmac copy1{hmac};
  OpenSSLHash::Hmac copy2 = hmac;

  hmac.hash_final(range(expected));
  copy1.hash_final(range(actual1));
  copy2.hash_final(range(actual2));

  EXPECT_EQ(expected, actual1);
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, hmac_sha256_hashmove) {
  std::array<uint8_t, 32> expected, actual1, actual2;

  constexpr StringPiece data{"foobar"};
  constexpr StringPiece key{"qwerty"};

  HMAC(
      EVP_sha256(),
      key.data(),
      int(key.size()),
      ByteRange{data}.data(),
      data.size(),
      expected.data(),
      nullptr);

  OpenSSLHash::Hmac hmac;
  hmac.hash_init(EVP_sha256(), {key});
  hmac.hash_update({data});

  OpenSSLHash::Hmac copy1{std::move(hmac)};
  copy1.hash_final(range(actual1));
  EXPECT_EQ(expected, actual1);

  hmac = OpenSSLHash::Hmac{};
  hmac.hash_init(EVP_sha256(), {key});
  hmac.hash_update({data});

  OpenSSLHash::Hmac copy2 = std::move(hmac);
  copy2.hash_final(range(actual2));
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, hmac_sha256_hashcopy_self) {
  std::array<uint8_t, 32> expected, actual;

  constexpr StringPiece data{"foobar"};
  constexpr StringPiece key{"qwerty"};

  HMAC(
      EVP_sha256(),
      key.data(),
      int(key.size()),
      ByteRange{data}.data(),
      data.size(),
      expected.data(),
      nullptr);

  OpenSSLHash::Hmac hmac;
  hmac.hash_init(EVP_sha256(), {key});
  hmac.hash_update({data});

  OpenSSLHash::Hmac* ptr = &hmac;
  hmac = *ptr;

  hmac.hash_final(range(actual));
  EXPECT_EQ(expected, actual);
}

TEST_F(OpenSSLHashTest, hmac_sha256_hashmove_self) {
  std::array<uint8_t, 32> expected, actual;

  constexpr StringPiece data{"foobar"};
  constexpr StringPiece key{"qwerty"};

  HMAC(
      EVP_sha256(),
      key.data(),
      int(key.size()),
      ByteRange{data}.data(),
      data.size(),
      expected.data(),
      nullptr);

  OpenSSLHash::Hmac hmac;
  hmac.hash_init(EVP_sha256(), {key});
  hmac.hash_update({data});

  OpenSSLHash::Hmac* ptr = &hmac;
  hmac = std::move(*ptr);

  hmac.hash_final(range(actual));
  EXPECT_EQ(expected, actual);
}

TEST_F(OpenSSLHashTest, hmac_sha256_hashcopy_from_default_constructed) {
  std::array<uint8_t, 32> expected, actual1, actual2;

  constexpr StringPiece data{"foobar"};
  constexpr StringPiece key{"qwerty"};

  HMAC(
      EVP_sha256(),
      key.data(),
      int(key.size()),
      ByteRange{data}.data(),
      data.size(),
      expected.data(),
      nullptr);

  OpenSSLHash::Hmac hmac;
  OpenSSLHash::Hmac copy1{hmac};
  OpenSSLHash::Hmac copy2 = hmac;

  copy1.hash_init(EVP_sha256(), {key});
  copy1.hash_update({data});
  copy1.hash_final(range(actual1));
  EXPECT_EQ(expected, actual1);

  copy2.hash_init(EVP_sha256(), {key});
  copy2.hash_update({data});
  copy2.hash_final(range(actual2));
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, hmac_sha256_hashmove_from_default_constructed) {
  std::array<uint8_t, 32> expected, actual1, actual2;

  constexpr StringPiece data{"foobar"};
  constexpr StringPiece key{"qwerty"};

  HMAC(
      EVP_sha256(),
      key.data(),
      int(key.size()),
      ByteRange{data}.data(),
      data.size(),
      expected.data(),
      nullptr);

  OpenSSLHash::Hmac hmac1;
  OpenSSLHash::Hmac copy1{std::move(hmac1)};
  OpenSSLHash::Hmac hmac2;
  OpenSSLHash::Hmac copy2 = std::move(hmac2);

  copy1.hash_init(EVP_sha256(), {key});
  copy1.hash_update({data});
  copy1.hash_final(range(actual1));
  EXPECT_EQ(expected, actual1);

  copy2.hash_init(EVP_sha256(), {key});
  copy2.hash_update({data});
  copy2.hash_final(range(actual2));
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, hmac_sha256_hashcopy_intermediate) {
  std::array<uint8_t, 32> expected, actual1, actual2;

  constexpr StringPiece data1{"foo"};
  constexpr StringPiece data2{"bar"};
  constexpr StringPiece key{"qwerty"};

  OpenSSLHash::Hmac hmac;
  hmac.hash_init(EVP_sha256(), {key});
  hmac.hash_update({data1});

  OpenSSLHash::Hmac copy1{hmac};
  OpenSSLHash::Hmac copy2 = hmac;
  hmac.hash_update({data2});
  copy1.hash_update({data2});
  copy2.hash_update({data2});

  hmac.hash_final(range(expected));
  copy1.hash_final(range(actual1));
  copy2.hash_final(range(actual2));

  EXPECT_EQ(expected, actual1);
  EXPECT_EQ(expected, actual2);
}

TEST_F(OpenSSLHashTest, hmac_sha256_movecopy_intermediate) {
  std::array<uint8_t, 32> expected, actual1, actual2;

  constexpr StringPiece data{"foobar"};
  constexpr StringPiece data1{"foo"};
  constexpr StringPiece data2{"bar"};
  constexpr StringPiece key{"qwerty"};

  HMAC(
      EVP_sha256(),
      key.data(),
      int(key.size()),
      ByteRange{data}.data(),
      data.size(),
      expected.data(),
      nullptr);

  OpenSSLHash::Hmac hmac;
  hmac.hash_init(EVP_sha256(), {key});
  hmac.hash_update({data1});
  OpenSSLHash::Hmac copy1{std::move(hmac)};
  copy1.hash_update({data2});
  copy1.hash_final(range(actual1));
  EXPECT_EQ(expected, actual1);

  hmac.hash_init(EVP_sha256(), {key});
  hmac.hash_update({data1});
  OpenSSLHash::Hmac copy2 = std::move(hmac);
  copy2.hash_update({data2});
  copy2.hash_final(range(actual2));
  EXPECT_EQ(expected, actual2);

  hmac.hash_init(EVP_sha256(), {key});
}

TEST_F(OpenSSLHashTest, hmac_update_without_init_throws) {
  OpenSSLHash::Hmac hmac;
  EXPECT_THROW(hmac.hash_update(ByteRange{}), std::runtime_error);
}

TEST_F(OpenSSLHashTest, hmac_final_without_init_throws) {
  OpenSSLHash::Hmac hmac;
  std::array<uint8_t, 32> out;
  EXPECT_THROW(hmac.hash_final(range(out)), std::runtime_error);
}
