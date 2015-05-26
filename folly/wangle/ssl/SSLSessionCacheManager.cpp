/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/wangle/ssl/SSLSessionCacheManager.h>

#include <folly/wangle/ssl/SSLCacheProvider.h>
#include <folly/wangle/ssl/SSLStats.h>
#include <folly/wangle/ssl/SSLUtil.h>

#include <folly/io/async/EventBase.h>

#ifndef NO_LIB_GFLAGS
#include <gflags/gflags.h>
#endif

using std::string;
using std::shared_ptr;

namespace {

const uint32_t NUM_CACHE_BUCKETS = 16;

// We use the default ID generator which fills the maximum ID length
// for the protocol.  16 bytes for SSLv2 or 32 for SSLv3+
const int MIN_SESSION_ID_LENGTH = 16;

}

#ifndef NO_LIB_GFLAGS
DEFINE_bool(dcache_unit_test, false, "All VIPs share one session cache");
#else
const bool FLAGS_dcache_unit_test = false;
#endif

namespace folly {


int SSLSessionCacheManager::sExDataIndex_ = -1;
shared_ptr<ShardedLocalSSLSessionCache> SSLSessionCacheManager::sCache_;
std::mutex SSLSessionCacheManager::sCacheLock_;

LocalSSLSessionCache::LocalSSLSessionCache(uint32_t maxCacheSize,
                                           uint32_t cacheCullSize)
    : sessionCache(maxCacheSize, cacheCullSize) {
  sessionCache.setPruneHook(std::bind(
                              &LocalSSLSessionCache::pruneSessionCallback,
                              this, std::placeholders::_1,
                              std::placeholders::_2));
}

void LocalSSLSessionCache::pruneSessionCallback(const string& sessionId,
                                                SSL_SESSION* session) {
  VLOG(4) << "Free SSL session from local cache; id="
          << SSLUtil::hexlify(sessionId);
  SSL_SESSION_free(session);
  ++removedSessions_;
}


// SSLSessionCacheManager implementation

SSLSessionCacheManager::SSLSessionCacheManager(
  uint32_t maxCacheSize,
  uint32_t cacheCullSize,
  SSLContext* ctx,
  const folly::SocketAddress& sockaddr,
  const string& context,
  EventBase* eventBase,
  SSLStats* stats,
  const std::shared_ptr<SSLCacheProvider>& externalCache):
    ctx_(ctx),
    stats_(stats),
    externalCache_(externalCache) {

  SSL_CTX* sslCtx = ctx->getSSLCtx();

  SSLUtil::getSSLCtxExIndex(&sExDataIndex_);

  SSL_CTX_set_ex_data(sslCtx, sExDataIndex_, this);
  SSL_CTX_sess_set_new_cb(sslCtx, SSLSessionCacheManager::newSessionCallback);
  SSL_CTX_sess_set_get_cb(sslCtx, SSLSessionCacheManager::getSessionCallback);
  SSL_CTX_sess_set_remove_cb(sslCtx,
                             SSLSessionCacheManager::removeSessionCallback);
  if (!FLAGS_dcache_unit_test && !context.empty()) {
    // Use the passed in context
    SSL_CTX_set_session_id_context(sslCtx, (const uint8_t *)context.data(),
                                   std::min((int)context.length(),
                                            SSL_MAX_SSL_SESSION_ID_LENGTH));
  }

  SSL_CTX_set_session_cache_mode(sslCtx, SSL_SESS_CACHE_NO_INTERNAL
                                 | SSL_SESS_CACHE_SERVER);

  localCache_ = SSLSessionCacheManager::getLocalCache(maxCacheSize,
                                                      cacheCullSize);

  VLOG(2) << "On VipID=" << sockaddr.describe() << " context=" << context;
}

SSLSessionCacheManager::~SSLSessionCacheManager() {
}

void SSLSessionCacheManager::shutdown() {
  std::lock_guard<std::mutex> g(sCacheLock_);
  sCache_.reset();
}

shared_ptr<ShardedLocalSSLSessionCache> SSLSessionCacheManager::getLocalCache(
  uint32_t maxCacheSize,
  uint32_t cacheCullSize) {

  std::lock_guard<std::mutex> g(sCacheLock_);
  if (!sCache_) {
    sCache_.reset(new ShardedLocalSSLSessionCache(NUM_CACHE_BUCKETS,
                                                  maxCacheSize,
                                                  cacheCullSize));
  }
  return sCache_;
}

int SSLSessionCacheManager::newSessionCallback(SSL* ssl, SSL_SESSION* session) {
  SSLSessionCacheManager* manager = nullptr;
  SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
  manager = (SSLSessionCacheManager *)SSL_CTX_get_ex_data(ctx, sExDataIndex_);

  if (manager == nullptr) {
    LOG(FATAL) << "Null SSLSessionCacheManager in callback";
    return -1;
  }
  return manager->newSession(ssl, session);
}


int SSLSessionCacheManager::newSession(SSL* ssl, SSL_SESSION* session) {
  string sessionId((char*)session->session_id, session->session_id_length);
  VLOG(4) << "New SSL session; id=" << SSLUtil::hexlify(sessionId);

  if (stats_) {
    stats_->recordSSLSession(true /* new session */, false, false);
  }

  localCache_->storeSession(sessionId, session, stats_);

  if (externalCache_) {
    VLOG(4) << "New SSL session: send session to external cache; id=" <<
      SSLUtil::hexlify(sessionId);
    storeCacheRecord(sessionId, session);
  }

  return 1;
}

void SSLSessionCacheManager::removeSessionCallback(SSL_CTX* ctx,
                                                   SSL_SESSION* session) {
  SSLSessionCacheManager* manager = nullptr;
  manager = (SSLSessionCacheManager *)SSL_CTX_get_ex_data(ctx, sExDataIndex_);

  if (manager == nullptr) {
    LOG(FATAL) << "Null SSLSessionCacheManager in callback";
    return;
  }
  return manager->removeSession(ctx, session);
}

void SSLSessionCacheManager::removeSession(SSL_CTX* ctx,
                                           SSL_SESSION* session) {
  string sessionId((char*)session->session_id, session->session_id_length);

  // This hook is only called from SSL when the internal session cache needs to
  // flush sessions.  Since we run with the internal cache disabled, this should
  // never be called
  VLOG(3) << "Remove SSL session; id=" << SSLUtil::hexlify(sessionId);

  localCache_->removeSession(sessionId);

  if (stats_) {
    stats_->recordSSLSessionRemove();
  }
}

SSL_SESSION* SSLSessionCacheManager::getSessionCallback(SSL* ssl,
                                                        unsigned char* sess_id,
                                                        int id_len,
                                                        int* copyflag) {
  SSLSessionCacheManager* manager = nullptr;
  SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
  manager = (SSLSessionCacheManager *)SSL_CTX_get_ex_data(ctx, sExDataIndex_);

  if (manager == nullptr) {
    LOG(FATAL) << "Null SSLSessionCacheManager in callback";
    return nullptr;
  }
  return manager->getSession(ssl, sess_id, id_len, copyflag);
}

SSL_SESSION* SSLSessionCacheManager::getSession(SSL* ssl,
                                                unsigned char* session_id,
                                                int id_len,
                                                int* copyflag) {
  VLOG(7) << "SSL get session callback";
  SSL_SESSION* session = nullptr;
  bool foreign = false;
  char const* missReason = nullptr;

  if (id_len < MIN_SESSION_ID_LENGTH) {
    // We didn't generate this session so it's going to be a miss.
    // This doesn't get logged or counted in the stats.
    return nullptr;
  }
  string sessionId((char*)session_id, id_len);

  AsyncSSLSocket* sslSocket = AsyncSSLSocket::getFromSSL(ssl);

  assert(sslSocket != nullptr);

  // look it up in the local cache first
  session = localCache_->lookupSession(sessionId);
#ifdef SSL_SESSION_CB_WOULD_BLOCK
  if (session == nullptr && externalCache_) {
    // external cache might have the session
    foreign = true;
    if (!SSL_want_sess_cache_lookup(ssl)) {
      missReason = "reason: No async cache support;";
    } else {
      PendingLookupMap::iterator pit = pendingLookups_.find(sessionId);
      if (pit == pendingLookups_.end()) {
        auto result = pendingLookups_.emplace(sessionId, PendingLookup());
        // initiate fetch
        VLOG(4) << "Get SSL session [Pending]: Initiate Fetch; fd=" <<
          sslSocket->getFd() << " id=" << SSLUtil::hexlify(sessionId);
        if (lookupCacheRecord(sessionId, sslSocket)) {
          // response is pending
          *copyflag = SSL_SESSION_CB_WOULD_BLOCK;
          return nullptr;
        } else {
          missReason = "reason: failed to send lookup request;";
          pendingLookups_.erase(result.first);
        }
      } else {
        // A lookup was already initiated from this thread
        if (pit->second.request_in_progress) {
          // Someone else initiated the request, attach
          VLOG(4) << "Get SSL session [Pending]: Request in progess: attach; "
            "fd=" << sslSocket->getFd() << " id=" <<
            SSLUtil::hexlify(sessionId);
          std::unique_ptr<DelayedDestruction::DestructorGuard> dg(
            new DelayedDestruction::DestructorGuard(sslSocket));
          pit->second.waiters.push_back(
            std::make_pair(sslSocket, std::move(dg)));
          *copyflag = SSL_SESSION_CB_WOULD_BLOCK;
          return nullptr;
        }
        // request is complete
        session = pit->second.session; // nullptr if our friend didn't have it
        if (session != nullptr) {
          CRYPTO_add(&session->references, 1, CRYPTO_LOCK_SSL_SESSION);
        }
      }
    }
  }
#endif

  bool hit = (session != nullptr);
  if (stats_) {
    stats_->recordSSLSession(false, hit, foreign);
  }
  if (hit) {
    sslSocket->setSessionIDResumed(true);
  }

  VLOG(4) << "Get SSL session [" <<
    ((hit) ? "Hit" : "Miss") << "]: " <<
    ((foreign) ? "external" : "local") << " cache; " <<
    ((missReason != nullptr) ? missReason : "") << "fd=" <<
    sslSocket->getFd() << " id=" << SSLUtil::hexlify(sessionId);

  // We already bumped the refcount
  *copyflag = 0;

  return session;
}

bool SSLSessionCacheManager::storeCacheRecord(const string& sessionId,
                                              SSL_SESSION* session) {
  std::string sessionString;
  uint32_t sessionLen = i2d_SSL_SESSION(session, nullptr);
  sessionString.resize(sessionLen);
  uint8_t* cp = (uint8_t *)sessionString.data();
  i2d_SSL_SESSION(session, &cp);
  size_t expiration = SSL_CTX_get_timeout(ctx_->getSSLCtx());
  return externalCache_->setAsync(sessionId, sessionString,
                                  std::chrono::seconds(expiration));
}

bool SSLSessionCacheManager::lookupCacheRecord(const string& sessionId,
                                               AsyncSSLSocket* sslSocket) {
  auto cacheCtx = new SSLCacheProvider::CacheContext();
  cacheCtx->sessionId = sessionId;
  cacheCtx->session = nullptr;
  cacheCtx->sslSocket = sslSocket;
  cacheCtx->guard.reset(
      new DelayedDestruction::DestructorGuard(cacheCtx->sslSocket));
  cacheCtx->manager = this;
  bool res = externalCache_->getAsync(sessionId, cacheCtx);
  if (!res) {
    delete cacheCtx;
  }
  return res;
}

void SSLSessionCacheManager::restartSSLAccept(
    const SSLCacheProvider::CacheContext* cacheCtx) {
  PendingLookupMap::iterator pit = pendingLookups_.find(cacheCtx->sessionId);
  CHECK(pit != pendingLookups_.end());
  pit->second.request_in_progress = false;
  pit->second.session = cacheCtx->session;
  VLOG(7) << "Restart SSL accept";
  cacheCtx->sslSocket->restartSSLAccept();
  for (const auto& attachedLookup: pit->second.waiters) {
    // Wake up anyone else who was waiting for this session
    VLOG(4) << "Restart SSL accept (waiters) for fd=" <<
      attachedLookup.first->getFd();
    attachedLookup.first->restartSSLAccept();
  }
  pendingLookups_.erase(pit);
}

void SSLSessionCacheManager::onGetSuccess(
    SSLCacheProvider::CacheContext* cacheCtx,
    const std::string& value) {
  const uint8_t* cp = (uint8_t*)value.data();
  cacheCtx->session = d2i_SSL_SESSION(nullptr, &cp, value.length());
  restartSSLAccept(cacheCtx);

  /* Insert in the LRU after restarting all clients.  The stats logic
   * in getSession would treat this as a local hit otherwise.
   */
  localCache_->storeSession(cacheCtx->sessionId, cacheCtx->session, stats_);
  delete cacheCtx;
}

void SSLSessionCacheManager::onGetFailure(
    SSLCacheProvider::CacheContext* cacheCtx) {
  restartSSLAccept(cacheCtx);
  delete cacheCtx;
}

} // namespace
