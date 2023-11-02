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

#include <folly/ssl/OpenSSLCertUtils.h>

#include <folly/Format.h>
#include <folly/Range.h>
#include <folly/String.h>
#include <folly/container/Enumerate.h>
#include <folly/experimental/TestUtil.h>
#include <folly/portability/GTest.h>
#include <folly/portability/OpenSSL.h>
#include <folly/portability/Time.h>
#include <folly/ssl/Init.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

using namespace testing;
using namespace folly;
using folly::test::find_resource;

const char* kTestCertWithoutSan = "folly/ssl/test/tests-cert.pem";
const char* kTestCa = "folly/ssl/test/ca-cert.pem";

// Test key
const std::string kTestKey = folly::stripLeftMargin(R"(
  ----BEGIN EC PRIVATE KEY-----
  MHcCAQEEIBskFwVZ9miFN+SKCFZPe9WEuFGmP+fsecLUnsTN6bOcoAoGCCqGSM49
  AwEHoUQDQgAE7/f4YYOYunAM/VkmjDYDg3AWUgyyTIraWmmQZsnu0bYNV/lLLfNz
  CtHggxGSwEtEe40nNb9C8wQmHUvb7VBBlw==
  -----END EC PRIVATE KEY-----
)");
const std::string kTestCertWithSan = folly::stripLeftMargin(R"(
  -----BEGIN CERTIFICATE-----
  MIIDXDCCAkSgAwIBAgIBAjANBgkqhkiG9w0BAQsFADBQMQswCQYDVQQGEwJVUzEL
  MAkGA1UECAwCQ0ExDTALBgNVBAoMBEFzb3gxJTAjBgNVBAMMHEFzb3ggQ2VydGlm
  aWNhdGlvbiBBdXRob3JpdHkwHhcNMTcwMjEzMjMyMTAzWhcNNDQwNzAxMjMyMTAz
  WjAwMQswCQYDVQQGEwJVUzENMAsGA1UECgwEQXNveDESMBAGA1UEAwwJMTI3LjAu
  MC4xMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE7/f4YYOYunAM/VkmjDYDg3AW
  UgyyTIraWmmQZsnu0bYNV/lLLfNzCtHggxGSwEtEe40nNb9C8wQmHUvb7VBBl6OC
  ASowggEmMAkGA1UdEwQCMAAwLAYJYIZIAYb4QgENBB8WHU9wZW5TU0wgR2VuZXJh
  dGVkIENlcnRpZmljYXRlMB0GA1UdDgQWBBRx1kmdZEfXHmWLHpSDI0Lh8hmfwzAf
  BgNVHSMEGDAWgBQX3ykJKb97nxp/6UZJyDvts7noezAxBgNVHREEKjAoghJhbm90
  aGVyZXhhbXBsZS5jb22CEioudGhpcmRleGFtcGxlLmNvbTB4BggrBgEFBQcBAQRs
  MGowaAYIKwYBBQUHMAKGXGh0dHBzOi8vcGhhYnJpY2F0b3IuZmIuY29tL2RpZmZ1
  c2lvbi9GQkNPREUvYnJvd3NlL21hc3Rlci90aS90ZXN0X2NlcnRzL2NhX2NlcnQu
  cGVtP3ZpZXc9cmF3MA0GCSqGSIb3DQEBCwUAA4IBAQCj3FLjLMLudaFDiYo9pAPQ
  NBYNpG27aajQCvnEsYaMAGnNBxUUhv/E4xpnJEhatiCJWlPgGebdjXkpXYkLxnFj
  38UmpfZbNcvPPKxXmjIlkpYeFwcHTAUpFmMXVHdr8FjkDSN+qWHLllMFNAAqp0U6
  4VWjDlq9xCjzNw+8fdcEpwylpPrbNyQHqSO1k+DhM2qPuQfiWPmHe2PbJv8JB3no
  HWGi9SNe0FjtJM3066L0Gj8g/bFDo/pnyKguQyGkS7PaepK5/u5Y2fMMBO/m4+U0
  b9Yb0TvatsqL688CoZcSn73A0yAjptwbD/4HmcVlG2j/y8eTVpXisugu6Xz+QQGu
  -----END CERTIFICATE-----
)");

const std::string kTestCertBundle = folly::stripLeftMargin(R"(
  -----BEGIN CERTIFICATE-----
  MIIDgzCCAmugAwIBAgIJAIkcS3PQcCm+MA0GCSqGSIb3DQEBCwUAMFgxCzAJBgNV
  BAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQg
  Q29tcGFueSBMdGQxFDASBgNVBAMMC3Rlc3QgY2VydCAxMB4XDTE3MTAyMzIwNTcw
  M1oXDTE4MTAyMzIwNTcwM1owWDELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1
  bHQgQ2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEUMBIGA1UEAwwL
  dGVzdCBjZXJ0IDEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCplTzR
  6shdhVNbx5HFViiYDBjRYXCWiUeR0/0+XPkyI+DPIGAQ6Mre8WD03GPebYn7j3Lr
  JwgV06BJNvVCLDy0SJbf6ToxGfKWSLEWOoip32nIpb9qxURtx44NUvhChP54hhKI
  zAf8nNlS+qKUYbmixJHeUWO//8wNpsMKDkvtfVUZ6oVV3JPOOihJ+sQ0sIc5x+xk
  3eWfa0cNoZnxu4plQg2O4RlHOv8ruMW6BttpcqQ8I+Rxq+/YOhNQhX+6GZ1+Rs+f
  ddWXYNH6tFxsLIEbgCqHhLGw7g+JRms9R+CxLCpjmhYhR2xgl6KQu/Racr2T/17z
  897VfY7X94PmamidAgMBAAGjUDBOMB0GA1UdDgQWBBRHQvRr2p3/83y1yXiiVnnS
  zObpzTAfBgNVHSMEGDAWgBRHQvRr2p3/83y1yXiiVnnSzObpzTAMBgNVHRMEBTAD
  AQH/MA0GCSqGSIb3DQEBCwUAA4IBAQAk61K1sjrS7rrLnGND1o1Q6D2ebgb1wcfU
  WX+ZnhlkUxjSS1nHmaulMftpvzbgrOt7HWZKMXIpetnDSfksrGpw6QJ3VWFIJlH5
  P4x8//pVeI5jQd4W7gIl65tZOc5cEH8aqnzkaGP8YBx6BI6N8px1gZVgePVu3ebR
  eLdrWH2l4VishWOf6rO/ltQdTwRIqj08QNsWmSrRK2d7J/DGA6R9JkdyxeLdxqmB
  2BMwJ7IVR+bWuTzD9Zk5lZseIVFcIksxmQ8jJuZXUdN8WOT/65p9UnN+Cc6+Q7F4
  rlVz+ytcdvaf5mDeqFILDK6btWcUP2Vr1EfRDt/QBrU6OjAVQD+U
  -----END CERTIFICATE-----
  -----BEGIN CERTIFICATE-----
  MIIDgzCCAmugAwIBAgIJAPzrfjTkvHezMA0GCSqGSIb3DQEBCwUAMFgxCzAJBgNV
  BAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQg
  Q29tcGFueSBMdGQxFDASBgNVBAMMC3Rlc3QgY2VydCAyMB4XDTE3MTAyMzIwNTcx
  NloXDTE4MTAyMzIwNTcxNlowWDELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1
  bHQgQ2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEUMBIGA1UEAwwL
  dGVzdCBjZXJ0IDIwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCzy9G/
  NM7Llp+foYxug2Dqc3r9zWtb4PvbRqoz8W0ZRy0GkL3JtOfLWtlz+RCGa//mlGMA
  HLa+Qg77nnjuhO/KCCgQS9fxHY+zcv1VBwzsKmKcju4BCscsTLPsy0SJCXBXSgnH
  S4NMR/K+YozwdikEZRbU4VLJiw44CeJ1h74r2ElHYuOL0SpL8PSlv7kJu3/xWUiV
  L2iWk+y8yKIpCRQ9I7+L0kuhylZAmVBTKtgbdcLfERqQNNWAT7D+p/6CwNmpT9ei
  G2xJ0N4bt3w8kwcZ+IkGwei8Nadix+POe3WVU9K1VXVfoLZ9nNWKRnwIFP4Bsmld
  rP4Uy2IZuhrKE4BPAgMBAAGjUDBOMB0GA1UdDgQWBBQkmeMfPQaax9wCZL16jSSG
  XigBWjAfBgNVHSMEGDAWgBQkmeMfPQaax9wCZL16jSSGXigBWjAMBgNVHRMEBTAD
  AQH/MA0GCSqGSIb3DQEBCwUAA4IBAQCXzqxYp1FqMS2M+opCSPezgPDBdE2S9g6d
  HJHV5CLptGnu1vQIlyCXy/7X9b6Qq8UzuYyFacN/37tbNw6sGyTRfL8sEeFYfFoT
  GvgSrRqSM47ZBYx5jW/Uslkc5qbq+v4zeGCq5611stQKsJYIudu0+PjJmgtNF6en
  zTx8B6eS79GRN3/M7/kFLlxeZNCQpmKwvPp8P7JE4ZHUtuzQoKtjdt/etWpS76fV
  Akx7VhCFg/lw80tmgSclq885hYRYc6DOKfUubWOacKVfmHwL4oDiSffBonI7MoH8
  SJbzsCBpVd/tkDADZpxBQplGV7AaDBoNS0qvZHfH5x9R9R5lx9M+
  -----END CERTIFICATE-----
  -----BEGIN CERTIFICATE-----
  MIIDgzCCAmugAwIBAgIJAOzqPJDDfSKDMA0GCSqGSIb3DQEBCwUAMFgxCzAJBgNV
  BAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQg
  Q29tcGFueSBMdGQxFDASBgNVBAMMC3Rlc3QgY2VydCAzMB4XDTE3MTAyMzIwNTcy
  NVoXDTE4MTAyMzIwNTcyNVowWDELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1
  bHQgQ2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEUMBIGA1UEAwwL
  dGVzdCBjZXJ0IDMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDWqU2b
  eBzaOAja6od84hFfgvitOGrCYqLXMUXe0X7AlldzXV4zHaVyTKdEwDwvKDi5p9OF
  uTxSZkZ0JSPHZeH2/rHXidNMWdtiy5x/5ra1u9ctN7jHeboIxmdpfxoGq7s6cRA5
  oRh0bCNmw+Y7K+1RITmPloB7155RbrJYZR5MOFIaCnZV3j/icKjASTOg3ivXX4lx
  BoHGMYF8rl+51FIJsuXvnBgF+GhadMVSWl4Qy6gLliml1MgujlmFg9/1y/xzdWZg
  yyLI3tvw7fo/NN62u41VQBdCGdpvnVxU4ADu2/T0vhAS+Bh2CMK1OAAw61x1507S
  f68mab9s8at49qefAgMBAAGjUDBOMB0GA1UdDgQWBBQnn76Swsnld6Q1weLgpo/S
  tt0KeTAfBgNVHSMEGDAWgBQnn76Swsnld6Q1weLgpo/Stt0KeTAMBgNVHRMEBTAD
  AQH/MA0GCSqGSIb3DQEBCwUAA4IBAQCB0XANIWyP7DYROh6MFQLqeylngd9iUGNe
  BMT4pWu60p5ZX13kK/gbV/P2cayUkkWEMWpzKcIX70IkaB5y/OxVMXUXo94UupsM
  b1T736wHA0TLeL7yDj9OnMYj/qa2r8pAyEObI84KoWRGMHH9UPSRbVMVrhg/agBA
  LA6eZhwiGctkCy09kp+SFbUpv+SMyVp60UrPub6j68Hzd0FioGY01Os7nScuPNo0
  rl2S+G36bcem8Z5MOkJ0LEFi6ctK9JdLcHkr1SVavo3fsYZaIZraJxFGcYUVyLT+
  Rw7ydBokxHWsmVJczuRmEovXcTmgIphti234e7usKjw8M5mGwYfa
  -----END CERTIFICATE-----
)");

const std::map<std::string, std::string> testCertWithSanExts{
    {"1.3.6.1.5.5.7.1.1",
     "0h\x6\b+\x6\x1\x5\x5\a0\x2\x86\\https://phabricator.fb.com/diffusion/"
     "FBCODE/browse/master/ti/test_certs/ca_cert.pem?view=raw"},
    {"2.5.29.35",
     "\x80\x14\x17\xDF\x29\x9\x29\xBF\x7B\x9F\x1A\x7F\xE9\x46\x49\xC8\x3B\xED"
     "\xB3\xB9\xE8\x7B"},
    {"2.5.29.19", "\x0"},
    {"2.16.840.1.113730.1.13", "OpenSSL Generated Certificate"},
    {"2.5.29.17",
     "\x82\x12"
     "anotherexample.com\x82\x12*.thirdexample.com"},
    {"2.5.29.14",
     "\x71\xD6\x49\x9D\x64\x47\xD7\x1E\x65\x8B\x1E\x94\x83\x23\x42\xE1\xF2\x19"
     "\x9F\xC3"},
    {"1.2.3.4.1", "Custom Extension 1"},
    {"1.2.3.4.2", "Custom Extension 2"},
};

class OpenSSLCertUtilsTest : public TestWithParam<bool> {
 public:
  void SetUp() override {
    folly::ssl::init();

    if (GetParam()) {
      // Run the test with an polluted error stack.
      SSLerr(SSL_F_SSL3_READ_BYTES, SSL_R_SSL_HANDSHAKE_FAILURE);
    }
  }
};

INSTANTIATE_TEST_SUITE_P(OpenSSLCertUtilsTest, OpenSSLCertUtilsTest, Bool());

static folly::ssl::X509UniquePtr readCertFromFile(const std::string& filename) {
  auto path = find_resource(filename);
  folly::ssl::BioUniquePtr bio(BIO_new(BIO_s_file()));
  if (!bio) {
    throw std::runtime_error("Couldn't create BIO");
  }

  if (BIO_read_filename(bio.get(), path.c_str()) != 1) {
    throw std::runtime_error("Couldn't read cert file: " + filename);
  }
  return folly::ssl::X509UniquePtr(
      PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

static folly::ssl::X509UniquePtr readCertFromData(
    const folly::StringPiece data) {
  folly::ssl::BioUniquePtr bio(BIO_new_mem_buf(data.data(), data.size()));
  if (!bio) {
    throw std::runtime_error("Couldn't create BIO");
  }
  return folly::ssl::X509UniquePtr(
      PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

// Validate the certs parsed from kTestCertBundle buffer.
static void validateTestCertBundle(
    const std::vector<folly::ssl::X509UniquePtr>& certs) {
  EXPECT_EQ(certs.size(), 3);
  for (auto i : folly::enumerate(certs)) {
    auto cn = folly::ssl::OpenSSLCertUtils::getCommonName(**i);
    EXPECT_TRUE(cn);
    EXPECT_EQ(*cn, folly::sformat("test cert {}", i.index + 1));
  }
}

// Validate parsed cert from kTestCertWithSan.
static void validateTestCertWithSAN(X509* x509) {
  ASSERT_NE(nullptr, x509);
  auto cn = folly::ssl::OpenSSLCertUtils::getCommonName(*x509);
  EXPECT_EQ("127.0.0.1", cn.value());
  auto altNames = folly::ssl::OpenSSLCertUtils::getSubjectAltNames(*x509);
  EXPECT_EQ(2, altNames.size());
  EXPECT_EQ("anotherexample.com", altNames[0]);
  EXPECT_EQ("*.thirdexample.com", altNames[1]);
}

static void addCustomExt(
    X509* x509, const std::string& oid, const std::string& data) {
  std::string extValue("\x0C");
  extValue.push_back(static_cast<char>(data.length()));
  extValue.append(data);
  folly::ssl::ASN1StrUniquePtr asn1String(ASN1_UTF8STRING_new());
  ASN1_STRING_set(asn1String.get(), extValue.c_str(), extValue.size());
  folly::ssl::ASN1ObjUniquePtr object(OBJ_txt2obj(oid.c_str(), 1));
  folly::ssl::X509ExtensionUniquePtr ext(X509_EXTENSION_create_by_OBJ(
      nullptr, object.get(), false, asn1String.get()));
  if (!ext) {
    throw std::runtime_error(
        folly::to<std::string>("Could not create extension ", oid));
  }
  if (!X509_add_ext(x509, ext.get(), -1)) {
    throw std::runtime_error(
        folly::to<std::string>("Could not add extension ", oid));
  }
}

TEST_P(OpenSSLCertUtilsTest, TestX509CN) {
  auto x509 = readCertFromFile(kTestCertWithoutSan);
  EXPECT_NE(x509, nullptr);
  auto cn = folly::ssl::OpenSSLCertUtils::getCommonName(*x509);
  EXPECT_EQ(cn.value(), "Asox Company");
  auto sans = folly::ssl::OpenSSLCertUtils::getSubjectAltNames(*x509);
  EXPECT_EQ(sans.size(), 0);
}

TEST_P(OpenSSLCertUtilsTest, TestX509Sans) {
  auto x509 = readCertFromData(kTestCertWithSan);
  validateTestCertWithSAN(x509.get());
}

TEST_P(OpenSSLCertUtilsTest, TestX509IssuerAndSubject) {
  auto x509 = readCertFromData(kTestCertWithSan);
  EXPECT_NE(x509, nullptr);
  auto issuer = folly::ssl::OpenSSLCertUtils::getIssuer(*x509);
  EXPECT_EQ(
      issuer.value(),
      "C = US, ST = CA, O = Asox, CN = Asox Certification Authority");
  auto subj = folly::ssl::OpenSSLCertUtils::getSubject(*x509);
  EXPECT_EQ(subj.value(), "C = US, O = Asox, CN = 127.0.0.1");
}

TEST_P(OpenSSLCertUtilsTest, TestX509Dates) {
  auto x509 = readCertFromData(kTestCertWithSan);
  EXPECT_NE(x509, nullptr);
  auto notBefore = folly::ssl::OpenSSLCertUtils::getNotBeforeTime(*x509);
  EXPECT_EQ(notBefore, "Feb 13 23:21:03 2017 GMT");
  auto notAfter = folly::ssl::OpenSSLCertUtils::getNotAfterTime(*x509);
  EXPECT_EQ(notAfter, "Jul  1 23:21:03 2044 GMT");
}

TEST_P(OpenSSLCertUtilsTest, TestASN1TimeToTimePoint) {
  auto x509 = readCertFromData(kTestCertWithSan);
  EXPECT_NE(x509, nullptr);
  std::tm tm = {};
  strptime("Feb 13 23:21:03 2017", "%b %d %H:%M:%S %Y", &tm);
  auto expected = std::chrono::system_clock::from_time_t(timegm(&tm));
  auto notBefore = X509_get_notBefore(x509.get());
  auto result = folly::ssl::OpenSSLCertUtils::asnTimeToTimepoint(notBefore);
  EXPECT_EQ(
      std::chrono::time_point_cast<std::chrono::seconds>(expected),
      std::chrono::time_point_cast<std::chrono::seconds>(result));
}

TEST_P(OpenSSLCertUtilsTest, TestX509Summary) {
  auto x509 = readCertFromData(kTestCertWithSan);
  EXPECT_NE(x509, nullptr);
  auto summary = folly::ssl::OpenSSLCertUtils::toString(*x509);
  EXPECT_EQ(
      summary.value(),
      "        Version: 3 (0x2)\n        Serial Number: 2 (0x2)\n"
      "        Issuer: C = US, ST = CA, O = Asox, CN = Asox Certification Authority\n"
      "        Validity\n            Not Before: Feb 13 23:21:03 2017 GMT\n"
      "            Not After : Jul  1 23:21:03 2044 GMT\n"
      "        Subject: C = US, O = Asox, CN = 127.0.0.1\n"
      "        X509v3 extensions:\n"
      "            X509v3 Basic Constraints: \n"
      "                CA:FALSE\n"
      "            Netscape Comment: \n"
      "                OpenSSL Generated Certificate\n"
      "            X509v3 Subject Key Identifier: \n"
      "                71:D6:49:9D:64:47:D7:1E:65:8B:1E:94:83:23:42:E1:F2:19:9F:C3\n"
      "            X509v3 Authority Key Identifier: \n"
      "                keyid:17:DF:29:09:29:BF:7B:9F:1A:7F:E9:46:49:C8:3B:ED:B3:B9:E8:7B\n\n"
      "            X509v3 Subject Alternative Name: \n"
      "                DNS:anotherexample.com, DNS:*.thirdexample.com\n"
      "            Authority Information Access: \n"
      "                CA Issuers - URI:https://phabricator.fb.com/diffusion/FBCODE/browse/master/ti/test_certs/ca_cert.pem?view=raw\n\n");
}

TEST_P(OpenSSLCertUtilsTest, TestDerEncodeDecode) {
  auto x509 = readCertFromData(kTestCertWithSan);

  auto der = folly::ssl::OpenSSLCertUtils::derEncode(*x509);
  auto decoded = folly::ssl::OpenSSLCertUtils::derDecode(der->coalesce());

  EXPECT_EQ(
      folly::ssl::OpenSSLCertUtils::toString(*x509),
      folly::ssl::OpenSSLCertUtils::toString(*decoded));
}

TEST_P(OpenSSLCertUtilsTest, TestDerDecodeJunkData) {
  StringPiece junk{"MyFakeCertificate"};
  EXPECT_THROW(
      folly::ssl::OpenSSLCertUtils::derDecode(junk), std::runtime_error);
}

TEST_P(OpenSSLCertUtilsTest, TestDerDecodeTooShort) {
  auto x509 = readCertFromData(kTestCertWithSan);

  auto der = folly::ssl::OpenSSLCertUtils::derEncode(*x509);
  der->trimEnd(1);
  EXPECT_THROW(
      folly::ssl::OpenSSLCertUtils::derDecode(der->coalesce()),
      std::runtime_error);
}

TEST_P(OpenSSLCertUtilsTest, TestReadCertsFromBuffer) {
  auto certs = folly::ssl::OpenSSLCertUtils::readCertsFromBuffer(
      StringPiece(kTestCertBundle));
  validateTestCertBundle(certs);
}

// readCertsFromBuffer() should manage to read certs from a buffer that contain
// both cert and private key.
TEST_P(OpenSSLCertUtilsTest, TestReadCertsFromMixedBuffer) {
  std::vector<std::string> bufs(
      {folly::to<std::string>(kTestCertWithSan, "\n\n", kTestKey, "\n"),
       folly::to<std::string>(kTestKey, "\n\n", kTestCertWithSan, "\n")});
  for (auto& buf : bufs) {
    auto certs = folly::ssl::OpenSSLCertUtils::readCertsFromBuffer(
        folly::StringPiece(buf));
    ASSERT_EQ(1, certs.size());
    validateTestCertWithSAN(certs.front().get());
  }
}

TEST_P(OpenSSLCertUtilsTest, TestX509Digest) {
  auto x509 = readCertFromData(kTestCertWithSan);
  EXPECT_NE(x509, nullptr);

  auto sha1Digest = folly::ssl::OpenSSLCertUtils::getDigestSha1(*x509);
  EXPECT_EQ(
      folly::hexlify(folly::range(sha1Digest)),
      "692c24cbc0f595b668480a8b62e5cd6bf5041c64");

  auto sha2Digest = folly::ssl::OpenSSLCertUtils::getDigestSha256(*x509);
  EXPECT_EQ(
      folly::hexlify(folly::range(sha2Digest)),
      "82f800877c004fc8e5d89d6646b15d135d2d4677d56dfcdf29a4698fce1c257f");
}

TEST_P(OpenSSLCertUtilsTest, TestX509Store) {
  auto store = folly::ssl::OpenSSLCertUtils::readStoreFromFile(
      find_resource(kTestCa).string());
  EXPECT_NE(store, nullptr);

  auto x509 = readCertFromFile(kTestCertWithoutSan);
  folly::ssl::X509StoreCtxUniquePtr ctx(X509_STORE_CTX_new());
  auto rc = X509_STORE_CTX_init(ctx.get(), store.get(), x509.get(), nullptr);
  EXPECT_EQ(rc, 1);
  rc = X509_verify_cert(ctx.get());
  EXPECT_EQ(rc, 1);
}

TEST_P(OpenSSLCertUtilsTest, TestProcessMalformedCertBuf) {
  std::string badCert =
      "-----BEGIN CERTIFICATE-----\n"
      "yo\n"
      "-----END CERTIFICATE-----\n";

  EXPECT_THROW(
      folly::ssl::OpenSSLCertUtils::readCertsFromBuffer(
          folly::StringPiece(badCert)),
      std::runtime_error);

  EXPECT_THROW(
      folly::ssl::OpenSSLCertUtils::readStoreFromBuffer(
          folly::StringPiece(badCert)),
      std::runtime_error);

  std::string bufWithBadCert =
      folly::to<std::string>(badCert, "\n", kTestCertBundle);

  EXPECT_THROW(
      folly::ssl::OpenSSLCertUtils::readCertsFromBuffer(
          folly::StringPiece(bufWithBadCert)),
      std::runtime_error);

  EXPECT_THROW(
      folly::ssl::OpenSSLCertUtils::readStoreFromBuffer(
          folly::StringPiece(bufWithBadCert)),
      std::runtime_error);
}

TEST_P(OpenSSLCertUtilsTest, TestReadStoreDuplicate) {
  auto dupBundle =
      folly::to<std::string>(kTestCertBundle, "\n\n", kTestCertBundle);

  auto store = folly::ssl::OpenSSLCertUtils::readStoreFromBuffer(
      folly::StringPiece(dupBundle));
  EXPECT_NE(store, nullptr);
  EXPECT_EQ(ERR_get_error(), 0);
}

TEST_P(OpenSSLCertUtilsTest, TestAllExtensions) {
  auto x509 = readCertFromData(kTestCertWithSan);
  EXPECT_NE(x509, nullptr);
  // Adding a couple of curtom extensions
  addCustomExt(x509.get(), "1.2.3.4.1", "Custom Extension 1");
  addCustomExt(x509.get(), "1.2.3.4.2", "Custom Extension 2");

  std::vector<std::pair<std::string, std::string>> extensions =
      folly::ssl::OpenSSLCertUtils::getAllExtensions(*x509);
  for (auto const& pair : extensions) {
    std::string name = pair.first;
    std::string value = pair.second;
    if (testCertWithSanExts.find(name) != testCertWithSanExts.end()) {
      EXPECT_EQ(value, testCertWithSanExts.find(name)->second);
    }
  }
}

TEST_P(OpenSSLCertUtilsTest, TestGetExtension) {
  auto x509 = readCertFromData(kTestCertWithSan);
  EXPECT_NE(x509, nullptr);
  addCustomExt(x509.get(), "1.2.3.4.1", "Custom Extension 1");
  addCustomExt(x509.get(), "1.2.3.4.2", "Custom Extension 2");
  for (const auto& [name, value] : testCertWithSanExts) {
    std::vector<std::string> extensionValues =
        folly::ssl::OpenSSLCertUtils::getExtension(*x509, name);
    EXPECT_EQ(extensionValues.size(), 1);
    EXPECT_EQ(extensionValues[0], value);
  }
}
