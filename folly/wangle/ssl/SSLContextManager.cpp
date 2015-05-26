/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/wangle/ssl/SSLContextManager.h>

#include <folly/wangle/ssl/ClientHelloExtStats.h>
#include <folly/wangle/ssl/DHParam.h>
#include <folly/wangle/ssl/PasswordInFile.h>
#include <folly/wangle/ssl/SSLCacheOptions.h>
#include <folly/wangle/ssl/SSLSessionCacheManager.h>
#include <folly/wangle/ssl/SSLUtil.h>
#include <folly/wangle/ssl/TLSTicketKeyManager.h>
#include <folly/wangle/ssl/TLSTicketKeySeeds.h>

#include <folly/Conv.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <functional>
#include <openssl/asn1.h>
#include <openssl/ssl.h>
#include <string>
#include <folly/io/async/EventBase.h>

#define OPENSSL_MISSING_FEATURE(name) \
do { \
  throw std::runtime_error("missing " #name " support in openssl");  \
} while(0)


using std::string;
using std::shared_ptr;

/**
 * SSLContextManager helps to create and manage all SSL_CTX,
 * SSLSessionCacheManager and TLSTicketManager for a listening
 * VIP:PORT. (Note, in SNI, a listening VIP:PORT can have >1 SSL_CTX(s)).
 *
 * Other responsibilities:
 * 1. It also handles the SSL_CTX selection after getting the tlsext_hostname
 *    in the client hello message.
 *
 * Usage:
 * 1. Each listening VIP:PORT serving SSL should have one SSLContextManager.
 *    It maps to Acceptor in the wangle vocabulary.
 *
 * 2. Create a SSLContextConfig object (e.g. by parsing the JSON config).
 *
 * 3. Call SSLContextManager::addSSLContextConfig() which will
 *    then create and configure the SSL_CTX
 *
 * Note: Each Acceptor, with SSL support, should have one SSLContextManager to
 * manage all SSL_CTX for the VIP:PORT.
 */

namespace folly {

namespace {

X509* getX509(SSL_CTX* ctx) {
  SSL* ssl = SSL_new(ctx);
  SSL_set_connect_state(ssl);
  X509* x509 = SSL_get_certificate(ssl);
  CRYPTO_add(&x509->references, 1, CRYPTO_LOCK_X509);
  SSL_free(ssl);
  return x509;
}

void set_key_from_curve(SSL_CTX* ctx, const std::string& curveName) {
#if OPENSSL_VERSION_NUMBER >= 0x0090800fL
#ifndef OPENSSL_NO_ECDH
  EC_KEY* ecdh = nullptr;
  int nid;

  /*
   * Elliptic-Curve Diffie-Hellman parameters are either "named curves"
   * from RFC 4492 section 5.1.1, or explicitly described curves over
   * binary fields. OpenSSL only supports the "named curves", which provide
   * maximum interoperability.
   */

  nid = OBJ_sn2nid(curveName.c_str());
  if (nid == 0) {
    LOG(FATAL) << "Unknown curve name:" << curveName.c_str();
    return;
  }
  ecdh = EC_KEY_new_by_curve_name(nid);
  if (ecdh == nullptr) {
    LOG(FATAL) << "Unable to create curve:" << curveName.c_str();
    return;
  }

  SSL_CTX_set_tmp_ecdh(ctx, ecdh);
  EC_KEY_free(ecdh);
#endif
#endif
}

// Helper to create TLSTicketKeyManger and aware of the needed openssl
// version/feature.
std::unique_ptr<TLSTicketKeyManager> createTicketManagerHelper(
  std::shared_ptr<folly::SSLContext> ctx,
  const TLSTicketKeySeeds* ticketSeeds,
  const SSLContextConfig& ctxConfig,
  SSLStats* stats) {

  std::unique_ptr<TLSTicketKeyManager> ticketManager;
#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
  if (ticketSeeds && ctxConfig.sessionTicketEnabled) {
    ticketManager = folly::make_unique<TLSTicketKeyManager>(ctx.get(), stats);
    ticketManager->setTLSTicketKeySeeds(
      ticketSeeds->oldSeeds,
      ticketSeeds->currentSeeds,
      ticketSeeds->newSeeds);
  } else {
    ctx->setOptions(SSL_OP_NO_TICKET);
  }
#else
  if (ticketSeeds && ctxConfig.sessionTicketEnabled) {
    OPENSSL_MISSING_FEATURE(TLSTicket);
  }
#endif
  return ticketManager;
}

std::string flattenList(const std::list<std::string>& list) {
  std::string s;
  bool first = true;
  for (auto& item : list) {
    if (first) {
      first = false;
    } else {
      s.append(", ");
    }
    s.append(item);
  }
  return s;
}

}

SSLContextManager::~SSLContextManager() {}

SSLContextManager::SSLContextManager(
  EventBase* eventBase,
  const std::string& vipName,
  bool strict,
  SSLStats* stats) :
    stats_(stats),
    eventBase_(eventBase),
    strict_(strict) {
}

void SSLContextManager::addSSLContextConfig(
  const SSLContextConfig& ctxConfig,
  const SSLCacheOptions& cacheOptions,
  const TLSTicketKeySeeds* ticketSeeds,
  const folly::SocketAddress& vipAddress,
  const std::shared_ptr<SSLCacheProvider>& externalCache) {

  unsigned numCerts = 0;
  std::string commonName;
  std::string lastCertPath;
  std::unique_ptr<std::list<std::string>> subjectAltName;
  auto sslCtx = std::make_shared<SSLContext>(ctxConfig.sslVersion);
  for (const auto& cert : ctxConfig.certificates) {
    try {
      sslCtx->loadCertificate(cert.certPath.c_str());
    } catch (const std::exception& ex) {
      // The exception isn't very useful without the certificate path name,
      // so throw a new exception that includes the path to the certificate.
      string msg = folly::to<string>("error loading SSL certificate ",
                                     cert.certPath, ": ",
                                     folly::exceptionStr(ex));
      LOG(ERROR) << msg;
      throw std::runtime_error(msg);
    }

    // Verify that the Common Name and (if present) Subject Alternative Names
    // are the same for all the certs specified for the SSL context.
    numCerts++;
    X509* x509 = getX509(sslCtx->getSSLCtx());
    auto guard = folly::makeGuard([x509] { X509_free(x509); });
    auto cn = SSLUtil::getCommonName(x509);
    if (!cn) {
      throw std::runtime_error(folly::to<string>("Cannot get CN for X509 ",
                                                 cert.certPath));
    }
    auto altName = SSLUtil::getSubjectAltName(x509);
    VLOG(2) << "cert " << cert.certPath << " CN: " << *cn;
    if (altName) {
      altName->sort();
      VLOG(2) << "cert " << cert.certPath << " SAN: " << flattenList(*altName);
    } else {
      VLOG(2) << "cert " << cert.certPath << " SAN: " << "{none}";
    }
    if (numCerts == 1) {
      commonName = *cn;
      subjectAltName = std::move(altName);
    } else {
      if (commonName != *cn) {
        throw std::runtime_error(folly::to<string>("X509 ", cert.certPath,
                                          " does not have same CN as ",
                                          lastCertPath));
      }
      if (altName == nullptr) {
        if (subjectAltName != nullptr) {
          throw std::runtime_error(folly::to<string>("X509 ", cert.certPath,
                                            " does not have same SAN as ",
                                            lastCertPath));
        }
      } else {
        if ((subjectAltName == nullptr) || (*altName != *subjectAltName)) {
          throw std::runtime_error(folly::to<string>("X509 ", cert.certPath,
                                            " does not have same SAN as ",
                                            lastCertPath));
        }
      }
    }
    lastCertPath = cert.certPath;

    // TODO t4438250 - Add ECDSA support to the crypto_ssl offload server
    //                 so we can avoid storing the ECDSA private key in the
    //                 address space of the Internet-facing process.  For
    //                 now, if cert name includes "-EC" to denote elliptic
    //                 curve, we load its private key even if the server as
    //                 a whole has been configured for async crypto.
    if (ctxConfig.isLocalPrivateKey ||
        (cert.certPath.find("-EC") != std::string::npos)) {
      // The private key lives in the same process

      // This needs to be called before loadPrivateKey().
      if (!cert.passwordPath.empty()) {
        auto sslPassword = std::make_shared<PasswordInFile>(cert.passwordPath);
        sslCtx->passwordCollector(sslPassword);
      }

      try {
        sslCtx->loadPrivateKey(cert.keyPath.c_str());
      } catch (const std::exception& ex) {
        // Throw an error that includes the key path, so the user can tell
        // which key had a problem.
        string msg = folly::to<string>("error loading private SSL key ",
                                       cert.keyPath, ": ",
                                       folly::exceptionStr(ex));
        LOG(ERROR) << msg;
        throw std::runtime_error(msg);
      }
    }
  }
  if (!ctxConfig.isLocalPrivateKey) {
    enableAsyncCrypto(sslCtx);
  }

  // Let the server pick the highest performing cipher from among the client's
  // choices.
  //
  // Let's use a unique private key for all DH key exchanges.
  //
  // Because some old implementations choke on empty fragments, most SSL
  // applications disable them (it's part of SSL_OP_ALL).  This
  // will improve performance and decrease write buffer fragmentation.
  sslCtx->setOptions(SSL_OP_CIPHER_SERVER_PREFERENCE |
    SSL_OP_SINGLE_DH_USE |
    SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS);

  // Configure SSL ciphers list
  if (!ctxConfig.tls11Ciphers.empty()) {
    // FIXME: create a dummy SSL_CTX for cipher testing purpose? It can
    //        remove the ordering dependency

    // Test to see if the specified TLS1.1 ciphers are valid.  Note that
    // these will be overwritten by the ciphers() call below.
    sslCtx->setCiphersOrThrow(ctxConfig.tls11Ciphers);
  }

  // Important that we do this *after* checking the TLS1.1 ciphers above,
  // since we test their validity by actually setting them.
  sslCtx->ciphers(ctxConfig.sslCiphers);

  // Use a fix DH param
  DH* dh = get_dh2048();
  SSL_CTX_set_tmp_dh(sslCtx->getSSLCtx(), dh);
  DH_free(dh);

  const string& curve = ctxConfig.eccCurveName;
  if (!curve.empty()) {
    set_key_from_curve(sslCtx->getSSLCtx(), curve);
  }

  if (!ctxConfig.clientCAFile.empty()) {
    try {
      sslCtx->setVerificationOption(SSLContext::VERIFY_REQ_CLIENT_CERT);
      sslCtx->loadTrustedCertificates(ctxConfig.clientCAFile.c_str());
      sslCtx->loadClientCAList(ctxConfig.clientCAFile.c_str());
    } catch (const std::exception& ex) {
      string msg = folly::to<string>("error loading client CA",
                                     ctxConfig.clientCAFile, ": ",
                                     folly::exceptionStr(ex));
      LOG(ERROR) << msg;
      throw std::runtime_error(msg);
    }
  }

  // - start - SSL session cache config
  // the internal cache never does what we want (per-thread-per-vip).
  // Disable it.  SSLSessionCacheManager will set it appropriately.
  SSL_CTX_set_session_cache_mode(sslCtx->getSSLCtx(), SSL_SESS_CACHE_OFF);
  SSL_CTX_set_timeout(sslCtx->getSSLCtx(),
                      cacheOptions.sslCacheTimeout.count());
  std::unique_ptr<SSLSessionCacheManager> sessionCacheManager;
  if (ctxConfig.sessionCacheEnabled &&
      cacheOptions.maxSSLCacheSize > 0 &&
      cacheOptions.sslCacheFlushSize > 0) {
    sessionCacheManager =
      folly::make_unique<SSLSessionCacheManager>(
        cacheOptions.maxSSLCacheSize,
        cacheOptions.sslCacheFlushSize,
        sslCtx.get(),
        vipAddress,
        commonName,
        eventBase_,
        stats_,
        externalCache);
  }
  // - end - SSL session cache config

  std::unique_ptr<TLSTicketKeyManager> ticketManager =
    createTicketManagerHelper(sslCtx, ticketSeeds, ctxConfig, stats_);

  // finalize sslCtx setup by the individual features supported by openssl
  ctxSetupByOpensslFeature(sslCtx, ctxConfig);

  try {
    insert(sslCtx,
           std::move(sessionCacheManager),
           std::move(ticketManager),
           ctxConfig.isDefault);
  } catch (const std::exception& ex) {
    string msg = folly::to<string>("Error adding certificate : ",
                                   folly::exceptionStr(ex));
    LOG(ERROR) << msg;
    throw std::runtime_error(msg);
  }

}

#ifdef PROXYGEN_HAVE_SERVERNAMECALLBACK
SSLContext::ServerNameCallbackResult
SSLContextManager::serverNameCallback(SSL* ssl) {
  shared_ptr<SSLContext> ctx;

  const char* sn = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (!sn) {
    VLOG(6) << "Server Name (tlsext_hostname) is missing";
    if (clientHelloTLSExtStats_) {
      clientHelloTLSExtStats_->recordAbsentHostname();
    }
    return SSLContext::SERVER_NAME_NOT_FOUND;
  }
  size_t snLen = strlen(sn);
  VLOG(6) << "Server Name (SNI TLS extension): '" << sn << "' ";

  // FIXME: This code breaks the abstraction. Suggestion?
  AsyncSSLSocket* sslSocket = AsyncSSLSocket::getFromSSL(ssl);
  CHECK(sslSocket);

  DNString dnstr(sn, snLen);

  uint32_t count = 0;
  do {
    // Try exact match first
    ctx = getSSLCtx(dnstr);
    if (ctx) {
      sslSocket->switchServerSSLContext(ctx);
      if (clientHelloTLSExtStats_) {
        clientHelloTLSExtStats_->recordMatch();
      }
      return SSLContext::SERVER_NAME_FOUND;
    }

    ctx = getSSLCtxBySuffix(dnstr);
    if (ctx) {
      sslSocket->switchServerSSLContext(ctx);
      if (clientHelloTLSExtStats_) {
        clientHelloTLSExtStats_->recordMatch();
      }
      return SSLContext::SERVER_NAME_FOUND;
    }

    // Give the noMatchFn one chance to add the correct cert
  }
  while (count++ == 0 && noMatchFn_ && noMatchFn_(sn));

  VLOG(6) << folly::stringPrintf("Cannot find a SSL_CTX for \"%s\"", sn);

  if (clientHelloTLSExtStats_) {
    clientHelloTLSExtStats_->recordNotMatch();
  }
  return SSLContext::SERVER_NAME_NOT_FOUND;
}
#endif

// Consolidate all SSL_CTX setup which depends on openssl version/feature
void
SSLContextManager::ctxSetupByOpensslFeature(
  shared_ptr<folly::SSLContext> sslCtx,
  const SSLContextConfig& ctxConfig) {
  // Disable compression - profiling shows this to be very expensive in
  // terms of CPU and memory consumption.
  //
#ifdef SSL_OP_NO_COMPRESSION
  sslCtx->setOptions(SSL_OP_NO_COMPRESSION);
#endif

  // Enable early release of SSL buffers to reduce the memory footprint
#ifdef SSL_MODE_RELEASE_BUFFERS
 sslCtx->getSSLCtx()->mode |= SSL_MODE_RELEASE_BUFFERS;
#endif
#ifdef SSL_MODE_EARLY_RELEASE_BBIO
  sslCtx->getSSLCtx()->mode |=  SSL_MODE_EARLY_RELEASE_BBIO;
#endif

  // This number should (probably) correspond to HTTPSession::kMaxReadSize
  // For now, this number must also be large enough to accommodate our
  // largest certificate, because some older clients (IE6/7) require the
  // cert to be in a single fragment.
#ifdef SSL_CTRL_SET_MAX_SEND_FRAGMENT
  SSL_CTX_set_max_send_fragment(sslCtx->getSSLCtx(), 8000);
#endif

  // Specify cipher(s) to be used for TLS1.1 client
  if (!ctxConfig.tls11Ciphers.empty()) {
#ifdef PROXYGEN_HAVE_SERVERNAMECALLBACK
    // Specified TLS1.1 ciphers are valid
    sslCtx->addClientHelloCallback(
      std::bind(
        &SSLContext::switchCiphersIfTLS11,
        sslCtx.get(),
        std::placeholders::_1,
        ctxConfig.tls11Ciphers
      )
    );
#else
    OPENSSL_MISSING_FEATURE(SNI);
#endif
  }

  // NPN (Next Protocol Negotiation)
  if (!ctxConfig.nextProtocols.empty()) {
#ifdef OPENSSL_NPN_NEGOTIATED
    sslCtx->setRandomizedAdvertisedNextProtocols(ctxConfig.nextProtocols);
#else
    OPENSSL_MISSING_FEATURE(NPN);
#endif
  }

  // SNI
#ifdef PROXYGEN_HAVE_SERVERNAMECALLBACK
  noMatchFn_ = ctxConfig.sniNoMatchFn;
  if (ctxConfig.isDefault) {
    if (defaultCtx_) {
      throw std::runtime_error(">1 X509 is set as default");
    }

    defaultCtx_ = sslCtx;
    defaultCtx_->setServerNameCallback(
      std::bind(&SSLContextManager::serverNameCallback, this,
                std::placeholders::_1));
  }
#else
  if (ctxs_.size() > 1) {
    OPENSSL_MISSING_FEATURE(SNI);
  }
#endif
}

void
SSLContextManager::insert(shared_ptr<SSLContext> sslCtx,
                          std::unique_ptr<SSLSessionCacheManager> smanager,
                          std::unique_ptr<TLSTicketKeyManager> tmanager,
                          bool defaultFallback) {
  X509* x509 = getX509(sslCtx->getSSLCtx());
  auto guard = folly::makeGuard([x509] { X509_free(x509); });
  auto cn = SSLUtil::getCommonName(x509);
  if (!cn) {
    throw std::runtime_error("Cannot get CN");
  }

  /**
   * Some notes from RFC 2818. Only for future quick references in case of bugs
   *
   * RFC 2818 section 3.1:
   * "......
   * If a subjectAltName extension of type dNSName is present, that MUST
   * be used as the identity. Otherwise, the (most specific) Common Name
   * field in the Subject field of the certificate MUST be used. Although
   * the use of the Common Name is existing practice, it is deprecated and
   * Certification Authorities are encouraged to use the dNSName instead.
   * ......
   * In some cases, the URI is specified as an IP address rather than a
   * hostname. In this case, the iPAddress subjectAltName must be present
   * in the certificate and must exactly match the IP in the URI.
   * ......"
   */

  // Not sure if we ever get this kind of X509...
  // If we do, assume '*' is always in the CN and ignore all subject alternative
  // names.
  if (cn->length() == 1 && (*cn)[0] == '*') {
    if (!defaultFallback) {
      throw std::runtime_error("STAR X509 is not the default");
    }
    ctxs_.emplace_back(sslCtx);
    sessionCacheManagers_.emplace_back(std::move(smanager));
    ticketManagers_.emplace_back(std::move(tmanager));
    return;
  }

  // Insert by CN
  insertSSLCtxByDomainName(cn->c_str(), cn->length(), sslCtx);

  // Insert by subject alternative name(s)
  auto altNames = SSLUtil::getSubjectAltName(x509);
  if (altNames) {
    for (auto& name : *altNames) {
      insertSSLCtxByDomainName(name.c_str(), name.length(), sslCtx);
    }
  }

  ctxs_.emplace_back(sslCtx);
  sessionCacheManagers_.emplace_back(std::move(smanager));
  ticketManagers_.emplace_back(std::move(tmanager));
}

void
SSLContextManager::insertSSLCtxByDomainName(const char* dn, size_t len,
                                            shared_ptr<SSLContext> sslCtx) {
  try {
    insertSSLCtxByDomainNameImpl(dn, len, sslCtx);
  } catch (const std::runtime_error& ex) {
    if (strict_) {
      throw ex;
    } else {
      LOG(ERROR) << ex.what() << " DN=" << dn;
    }
  }
}
void
SSLContextManager::insertSSLCtxByDomainNameImpl(const char* dn, size_t len,
                                                shared_ptr<SSLContext> sslCtx)
{
  VLOG(4) <<
    folly::stringPrintf("Adding CN/Subject-alternative-name \"%s\" for "
                        "SNI search", dn);

  // Only support wildcard domains which are prefixed exactly by "*." .
  // "*" appearing at other locations is not accepted.

  if (len > 2 && dn[0] == '*') {
    if (dn[1] == '.') {
      // skip the first '*'
      dn++;
      len--;
    } else {
      throw std::runtime_error(
        "Invalid wildcard CN/subject-alternative-name \"" + std::string(dn) + "\" "
        "(only allow character \".\" after \"*\"");
    }
  }

  if (len == 1 && *dn == '.') {
    throw std::runtime_error("X509 has only '.' in the CN or subject alternative name "
                    "(after removing any preceding '*')");
  }

  if (strchr(dn, '*')) {
    throw std::runtime_error("X509 has '*' in the the CN or subject alternative name "
                    "(after removing any preceding '*')");
  }

  DNString dnstr(dn, len);
  const auto v = dnMap_.find(dnstr);
  if (v == dnMap_.end()) {
    dnMap_.emplace(dnstr, sslCtx);
  } else if (v->second == sslCtx) {
    VLOG(6)<< "Duplicate CN or subject alternative name found in the same X509."
      "  Ignore the later name.";
  } else {
    throw std::runtime_error("Duplicate CN or subject alternative name found: \"" +
                             std::string(dnstr.c_str()) + "\"");
  }
}

shared_ptr<SSLContext>
SSLContextManager::getSSLCtxBySuffix(const DNString& dnstr) const
{
  size_t dot;

  if ((dot = dnstr.find_first_of(".")) != DNString::npos) {
    DNString suffixDNStr(dnstr, dot);
    const auto v = dnMap_.find(suffixDNStr);
    if (v != dnMap_.end()) {
      VLOG(6) << folly::stringPrintf("\"%s\" is a willcard match to \"%s\"",
                                     dnstr.c_str(), suffixDNStr.c_str());
      return v->second;
    }
  }

  VLOG(6) << folly::stringPrintf("\"%s\" is not a wildcard match",
                                 dnstr.c_str());
  return shared_ptr<SSLContext>();
}

shared_ptr<SSLContext>
SSLContextManager::getSSLCtx(const DNString& dnstr) const
{
  const auto v = dnMap_.find(dnstr);
  if (v == dnMap_.end()) {
    VLOG(6) << folly::stringPrintf("\"%s\" is not an exact match",
                                   dnstr.c_str());
    return shared_ptr<SSLContext>();
  } else {
    VLOG(6) << folly::stringPrintf("\"%s\" is an exact match", dnstr.c_str());
    return v->second;
  }
}

shared_ptr<SSLContext>
SSLContextManager::getDefaultSSLCtx() const {
  return defaultCtx_;
}

void
SSLContextManager::reloadTLSTicketKeys(
  const std::vector<std::string>& oldSeeds,
  const std::vector<std::string>& currentSeeds,
  const std::vector<std::string>& newSeeds) {
#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
  for (auto& tmgr: ticketManagers_) {
    tmgr->setTLSTicketKeySeeds(oldSeeds, currentSeeds, newSeeds);
  }
#endif
}

} // namespace
