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

#include <folly/io/async/SSLContext.h>

#include <folly/FileUtil.h>
#include <folly/experimental/TestUtil.h>
#include <folly/io/async/test/SSLUtil.h>
#include <folly/portability/GTest.h>
#include <folly/portability/OpenSSL.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

#if !defined(FOLLY_CERTS_DIR)
#define FOLLY_CERTS_DIR "folly/io/async/test"
#endif

using namespace std;
using folly::test::find_resource;

namespace folly {

class SSLContextTest : public testing::Test {
 public:
  SSLContext ctx;
  void verifySSLCipherList(const vector<string>& ciphers);
  void verifySSLCiphersuites(const vector<string>& ciphersuites);
};

void SSLContextTest::verifySSLCipherList(const vector<string>& ciphers) {
  ssl::SSLUniquePtr ssl(ctx.createSSL());
  EXPECT_EQ(ciphers, test::getNonTLS13CipherList(ssl.get()));
}

void SSLContextTest::verifySSLCiphersuites(const vector<string>& ciphersuites) {
  ssl::SSLUniquePtr ssl(ctx.createSSL());
  EXPECT_EQ(ciphersuites, test::getTLS13Ciphersuites(ssl.get()));
}

TEST_F(SSLContextTest, TestSetCipherString) {
  ctx.ciphers("AES128-SHA:ECDHE-RSA-AES256-SHA384");
  verifySSLCipherList({"AES128-SHA", "ECDHE-RSA-AES256-SHA384"});
}

TEST_F(SSLContextTest, TestSetCipherList) {
  const vector<string> ciphers = {"ECDHE-RSA-AES128-SHA", "AES256-SHA"};
  ctx.setCipherList(ciphers);
  verifySSLCipherList(ciphers);
}

TEST_F(SSLContextTest, TestCipherRemoval) {
  ctx.setCipherList({"ECDHE-RSA-AES128-SHA", "AES256-SHA"});
  {
    ssl::SSLUniquePtr ssl(ctx.createSSL());
    auto ciphers = test::getCiphersFromSSL(ssl.get());
    EXPECT_TRUE(
        std::find(begin(ciphers), end(ciphers), "AES256-SHA") != end(ciphers));
  }

  ctx.setCipherList({"ECDHE-RSA-AES128-SHA"});
  {
    ssl::SSLUniquePtr ssl(ctx.createSSL());
    auto ciphers = test::getCiphersFromSSL(ssl.get());
    EXPECT_FALSE(
        std::find(begin(ciphers), end(ciphers), "AES256-SHA") != end(ciphers));
  }
}

TEST_F(SSLContextTest, TestLoadCertKey) {
  std::string certData, keyData, anotherKeyData;
  const char* certPath = FOLLY_CERTS_DIR "/tests-cert.pem";
  const char* keyPath = FOLLY_CERTS_DIR "/tests-key.pem";
  const char* anotherKeyPath = FOLLY_CERTS_DIR "/client_key.pem";
  folly::readFile(find_resource(certPath).c_str(), certData);
  folly::readFile(find_resource(keyPath).c_str(), keyData);
  folly::readFile(find_resource(anotherKeyPath).c_str(), anotherKeyData);

  {
    SCOPED_TRACE("Valid cert/key pair from buffer");
    SSLContext tmpCtx;
    tmpCtx.loadCertificateFromBufferPEM(certData);
    tmpCtx.loadPrivateKeyFromBufferPEM(keyData);
    EXPECT_TRUE(tmpCtx.isCertKeyPairValid());
  }

  {
    SCOPED_TRACE("Valid cert/key pair from files");
    SSLContext tmpCtx;
    tmpCtx.loadCertificate(find_resource(certPath).c_str());
    tmpCtx.loadPrivateKey(find_resource(keyPath).c_str());
    EXPECT_TRUE(tmpCtx.isCertKeyPairValid());
  }

  {
    SCOPED_TRACE("Invalid cert/key pair from file. Load cert first");
    SSLContext tmpCtx;
    tmpCtx.loadCertificate(find_resource(certPath).c_str());
    EXPECT_THROW(
        tmpCtx.loadPrivateKey(find_resource(anotherKeyPath).c_str()),
        std::runtime_error);
  }

  {
    SCOPED_TRACE("Invalid cert/key pair from file. Load key first");
    SSLContext tmpCtx;
    tmpCtx.loadPrivateKey(find_resource(anotherKeyPath).c_str());
    tmpCtx.loadCertificate(find_resource(certPath).c_str());
    EXPECT_FALSE(tmpCtx.isCertKeyPairValid());
  }

  {
    SCOPED_TRACE("Invalid key/cert pair from buf. Load cert first");
    SSLContext tmpCtx;
    tmpCtx.loadCertificateFromBufferPEM(certData);
    EXPECT_THROW(
        tmpCtx.loadPrivateKeyFromBufferPEM(anotherKeyData), std::runtime_error);
  }

  {
    SCOPED_TRACE("Invalid key/cert pair from buf. Load key first");
    SSLContext tmpCtx;
    tmpCtx.loadPrivateKeyFromBufferPEM(anotherKeyData);
    tmpCtx.loadCertificateFromBufferPEM(certData);
    EXPECT_FALSE(tmpCtx.isCertKeyPairValid());
  }

  {
    SCOPED_TRACE(
        "loadCertKeyPairFromBufferPEM() must throw when cert/key mismatch");
    SSLContext tmpCtx;
    EXPECT_THROW(
        tmpCtx.loadCertKeyPairFromBufferPEM(certData, anotherKeyData),
        std::runtime_error);
  }

  {
    SCOPED_TRACE(
        "loadCertKeyPairFromBufferPEM() must succeed when cert/key match");
    SSLContext tmpCtx;
    tmpCtx.loadCertKeyPairFromBufferPEM(certData, keyData);
  }

  {
    SCOPED_TRACE(
        "loadCertKeyPairFromFiles() must throw when cert/key mismatch");
    SSLContext tmpCtx;
    EXPECT_THROW(
        tmpCtx.loadCertKeyPairFromFiles(
            find_resource(certPath).c_str(),
            find_resource(anotherKeyPath).c_str()),
        std::runtime_error);
  }

  {
    SCOPED_TRACE("loadCertKeyPairFromFiles() must succeed when cert/key match");
    SSLContext tmpCtx;
    tmpCtx.loadCertKeyPairFromFiles(
        find_resource(certPath).c_str(), find_resource(keyPath).c_str());
  }
}

TEST_F(SSLContextTest, TestLoadCertificateChain) {
  constexpr auto kCertChainPath = FOLLY_CERTS_DIR "/client_chain.pem";
  auto path = find_resource(kCertChainPath);
  std::unique_ptr<SSLContext> ctx2;
  STACK_OF(X509) * stack;
  SSL_CTX* sctx;

  std::string contents;
  EXPECT_TRUE(folly::readFile(path.c_str(), contents));

  ctx2 = std::make_unique<SSLContext>();
  ctx2->loadCertificate(path.c_str(), "PEM");
  stack = nullptr;
  sctx = ctx2->getSSLCtx();
  SSL_CTX_get0_chain_certs(sctx, &stack);
  ASSERT_NE(stack, nullptr);
  EXPECT_EQ(1, sk_X509_num(stack));

  ctx2 = std::make_unique<SSLContext>();
  ctx2->loadCertificateFromBufferPEM(contents);
  stack = nullptr;
  sctx = ctx2->getSSLCtx();
  SSL_CTX_get0_chain_certs(sctx, &stack);
  ASSERT_NE(stack, nullptr);
  EXPECT_EQ(1, sk_X509_num(stack));
}

TEST_F(SSLContextTest, TestSetSupportedClientCAs) {
  constexpr auto kCertChainPath = FOLLY_CERTS_DIR "/client_chain.pem";
  ctx.setSupportedClientCertificateAuthorityNamesFromFile(
      find_resource(kCertChainPath).c_str());

  STACK_OF(X509_NAME)* names = SSL_CTX_get_client_CA_list(ctx.getSSLCtx());
  EXPECT_EQ(2, sk_X509_NAME_num(names));

  static const char* kExpectedCNs[] = {"Leaf Certificate", "Intermediate CA"};
  for (int i = 0; i < sk_X509_NAME_num(names); i++) {
    auto name = sk_X509_NAME_value(names, i);
    int indexCN = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    EXPECT_NE(indexCN, -1);

    auto entry = X509_NAME_get_entry(name, indexCN);
    ASSERT_NE(entry, nullptr);
    auto asnStringCN = X509_NAME_ENTRY_get_data(entry);
    std::string commonName(
        reinterpret_cast<const char*>(ASN1_STRING_get0_data(asnStringCN)),
        ASN1_STRING_length(asnStringCN));
    EXPECT_EQ(commonName, std::string(kExpectedCNs[i]));
  }
}

TEST_F(SSLContextTest, TestGetFromSSLCtx) {
  // Positive test
  SSLContext* contextPtr = SSLContext::getFromSSLCtx(ctx.getSSLCtx());
  EXPECT_EQ(contextPtr, &ctx);

  // Negative test
  SSL_CTX* randomCtx = SSL_CTX_new(TLS_method());
  EXPECT_EQ(nullptr, SSLContext::getFromSSLCtx(randomCtx));
  SSL_CTX_free(randomCtx);
}

#if OPENSSL_VERSION_NUMBER >= 0x1000200fL
TEST_F(SSLContextTest, TestInvalidSigAlgThrows) {
  {
    SSLContext tmpCtx;
    EXPECT_THROW(tmpCtx.setSigAlgsOrThrow(""), std::runtime_error);
  }

  {
    SSLContext tmpCtx;
    EXPECT_THROW(
        tmpCtx.setSigAlgsOrThrow("rsa_pss_rsae_sha512:ECDSA+SHA256:RSA+HA256"),
        std::runtime_error);
  }
}
#endif

#if FOLLY_OPENSSL_PREREQ(1, 1, 1)
TEST_F(SSLContextTest, TestSetCiphersuites) {
  std::vector<std::string> ciphersuitesList{
      "TLS_AES_128_CCM_SHA256",
      "TLS_AES_128_GCM_SHA256",
  };
  std::string ciphersuites;
  folly::join(":", ciphersuitesList, ciphersuites);
  ctx.setCiphersuitesOrThrow(ciphersuites);

  verifySSLCiphersuites(ciphersuitesList);
}

TEST_F(SSLContextTest, TestSetInvalidCiphersuite) {
  EXPECT_THROW(
      ctx.setCiphersuitesOrThrow("ECDHE-ECDSA-AES256-GCM-SHA384"),
      std::runtime_error);
}
#endif // FOLLY_OPENSSL_PREREQ(1, 1, 1)

#if FOLLY_OPENSSL_HAS_TLS13
TEST_F(SSLContextTest, TestTLS13MinVersion) {
  SSLContext sslContext{SSLContext::SSLVersion::TLSv1_3};
  int minProtoVersion = SSL_CTX_get_min_proto_version(sslContext.getSSLCtx());
  EXPECT_EQ(minProtoVersion, TLS1_3_VERSION);
}
#endif

TEST_F(SSLContextTest, AdvertisedNextProtocols) {
  EXPECT_EQ(ctx.getAdvertisedNextProtocols(), "");

  ctx.setAdvertisedNextProtocols({"blub"});
  EXPECT_EQ(ctx.getAdvertisedNextProtocols(), "blub");

  ctx.setAdvertisedNextProtocols({"foo", "bar", "baz"});
  EXPECT_EQ(ctx.getAdvertisedNextProtocols(), "foo,bar,baz");
}

} // namespace folly
