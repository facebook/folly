/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/wangle/ssl/SSLCacheProvider.h>
#include <folly/wangle/ssl/SSLStats.h>

#include <folly/EvictingCacheMap.h>
#include <mutex>
#include <folly/io/async/AsyncSSLSocket.h>

namespace folly {

class SSLStats;

/**
 * Basic SSL session cache map: Maps session id -> session
 */
typedef folly::EvictingCacheMap<std::string, SSL_SESSION*> SSLSessionCacheMap;

/**
 * Holds an SSLSessionCacheMap and associated lock
 */
class LocalSSLSessionCache: private boost::noncopyable {
 public:
  LocalSSLSessionCache(uint32_t maxCacheSize, uint32_t cacheCullSize);

  ~LocalSSLSessionCache() {
    std::lock_guard<std::mutex> g(lock);
    // EvictingCacheMap dtor doesn't free values
    sessionCache.clear();
  }

  SSLSessionCacheMap sessionCache;
  std::mutex lock;
  uint32_t removedSessions_{0};

 private:

  void pruneSessionCallback(const std::string& sessionId,
                            SSL_SESSION* session);
};

/**
 * A sharded LRU for SSL sessions.  The sharding is inteneded to reduce
 * contention for the LRU locks.  Assuming uniform distribution, two workers
 * will contend for the same lock with probability 1 / n_buckets^2.
 */
class ShardedLocalSSLSessionCache : private boost::noncopyable {
 public:
  ShardedLocalSSLSessionCache(uint32_t n_buckets, uint32_t maxCacheSize,
                              uint32_t cacheCullSize) {
    CHECK(n_buckets > 0);
    maxCacheSize = (uint32_t)(((double)maxCacheSize) / n_buckets);
    cacheCullSize = (uint32_t)(((double)cacheCullSize) / n_buckets);
    if (maxCacheSize == 0) {
      maxCacheSize = 1;
    }
    if (cacheCullSize == 0) {
      cacheCullSize = 1;
    }
    for (uint32_t i = 0; i < n_buckets; i++) {
      caches_.push_back(
        std::unique_ptr<LocalSSLSessionCache>(
          new LocalSSLSessionCache(maxCacheSize, cacheCullSize)));
    }
  }

  SSL_SESSION* lookupSession(const std::string& sessionId) {
    size_t bucket = hash(sessionId);
    SSL_SESSION* session = nullptr;
    std::lock_guard<std::mutex> g(caches_[bucket]->lock);

    auto itr = caches_[bucket]->sessionCache.find(sessionId);
    if (itr != caches_[bucket]->sessionCache.end()) {
      session = itr->second;
    }

    if (session) {
      CRYPTO_add(&session->references, 1, CRYPTO_LOCK_SSL_SESSION);
    }
    return session;
  }

  void storeSession(const std::string& sessionId, SSL_SESSION* session,
                    SSLStats* stats) {
    size_t bucket = hash(sessionId);
    SSL_SESSION* oldSession = nullptr;
    std::lock_guard<std::mutex> g(caches_[bucket]->lock);

    auto itr = caches_[bucket]->sessionCache.find(sessionId);
    if (itr != caches_[bucket]->sessionCache.end()) {
      oldSession = itr->second;
    }

    if (oldSession) {
      // LRUCacheMap doesn't free on overwrite, so 2x the work for us
      // This can happen in race conditions
      SSL_SESSION_free(oldSession);
    }
    caches_[bucket]->removedSessions_ = 0;
    caches_[bucket]->sessionCache.set(sessionId, session, true);
    if (stats) {
      stats->recordSSLSessionFree(caches_[bucket]->removedSessions_);
    }
  }

  void removeSession(const std::string& sessionId) {
    size_t bucket = hash(sessionId);
    std::lock_guard<std::mutex> g(caches_[bucket]->lock);
    caches_[bucket]->sessionCache.erase(sessionId);
  }

 private:

  /* SSL session IDs are 32 bytes of random data, hash based on first 16 bits */
  size_t hash(const std::string& key) {
    CHECK(key.length() >= 2);
    return (key[0] << 8 | key[1]) % caches_.size();
  }

  std::vector< std::unique_ptr<LocalSSLSessionCache> > caches_;
};

/* A socket/DestructorGuard pair */
typedef std::pair<AsyncSSLSocket *,
                  std::unique_ptr<DelayedDestruction::DestructorGuard>>
  AttachedLookup;

/**
 * PendingLookup structure
 *
 * Keeps track of clients waiting for an SSL session to be retrieved from
 * the external cache provider.
 */
struct PendingLookup {
  bool request_in_progress;
  SSL_SESSION* session;
  std::list<AttachedLookup> waiters;

  PendingLookup() {
    request_in_progress = true;
    session = nullptr;
  }
};

/* Maps SSL session id to a PendingLookup structure */
typedef std::map<std::string, PendingLookup> PendingLookupMap;

/**
 * SSLSessionCacheManager handles all stateful session caching.  There is an
 * instance of this object per SSL VIP per thread, with a 1:1 correlation with
 * SSL_CTX.  The cache can work locally or in concert with an external cache
 * to share sessions across instances.
 *
 * There is a single in memory session cache shared by all VIPs.  The cache is
 * split into N buckets (currently 16) with a separate lock per bucket.  The
 * VIP ID is hashed and stored as part of the session to handle the
 * (very unlikely) case of session ID collision.
 *
 * When a new SSL session is created, it is added to the LRU cache and
 * sent to the external cache to be stored.  The external cache
 * expiration is equal to the SSL session's expiration.
 *
 * When a resume request is received, SSLSessionCacheManager first looks in the
 * local LRU cache for the VIP.  If there is a miss there, an asynchronous
 * request for this session is dispatched to the external cache.  When the
 * external cache query returns, the LRU cache is updated if the session was
 * found, and the SSL_accept call is resumed.
 *
 * If additional resume requests for the same session ID arrive in the same
 * thread while the request is pending, the 2nd - Nth callers attach to the
 * original external cache requests and are resumed when it comes back.  No
 * attempt is made to coalesce external cache requests for the same session
 * ID in different worker threads.  Previous work did this, but the
 * complexity was deemed to outweigh the potential savings.
 *
 */
class SSLSessionCacheManager : private boost::noncopyable {
 public:
  /**
   * Constructor.  SSL session related callbacks will be set on the underlying
   * SSL_CTX.  vipId is assumed to a unique string identifying the VIP and must
   * be the same on all servers that wish to share sessions via the same
   * external cache.
   */
  SSLSessionCacheManager(
    uint32_t maxCacheSize,
    uint32_t cacheCullSize,
    SSLContext* ctx,
    const folly::SocketAddress& sockaddr,
    const std::string& context,
    EventBase* eventBase,
    SSLStats* stats,
    const std::shared_ptr<SSLCacheProvider>& externalCache);

  virtual ~SSLSessionCacheManager();

  /**
   * Call this on shutdown to release the global instance of the
   * ShardedLocalSSLSessionCache.
   */
  static void shutdown();

  /**
   * Callback for ExternalCache to call when an async get succeeds
   * @param context  The context that was passed to the async get request
   * @param value    Serialized session
   */
  void onGetSuccess(SSLCacheProvider::CacheContext* context,
                    const std::string& value);

  /**
   * Callback for ExternalCache to call when an async get fails, either
   * because the requested session is not in the external cache or because
   * of an error.
   * @param context  The context that was passed to the async get request
   */
  void onGetFailure(SSLCacheProvider::CacheContext* context);

 private:

  SSLContext* ctx_;
  std::shared_ptr<ShardedLocalSSLSessionCache> localCache_;
  PendingLookupMap pendingLookups_;
  SSLStats* stats_{nullptr};
  std::shared_ptr<SSLCacheProvider> externalCache_;

  /**
   * Invoked by openssl when a new SSL session is created
   */
  int newSession(SSL* ssl, SSL_SESSION* session);

  /**
   * Invoked by openssl when an SSL session is ejected from its internal cache.
   * This can't be invoked in the current implementation because SSL's internal
   * caching is disabled.
   */
  void removeSession(SSL_CTX* ctx, SSL_SESSION* session);

  /**
   * Invoked by openssl when a client requests a stateful session resumption.
   * Triggers a lookup in our local cache and potentially an asynchronous
   * request to an external cache.
   */
  SSL_SESSION* getSession(SSL* ssl, unsigned char* session_id,
                          int id_len, int* copyflag);

  /**
   * Store a new session record in the external cache
   */
  bool storeCacheRecord(const std::string& sessionId, SSL_SESSION* session);

  /**
   * Lookup a session in the external cache for the specified SSL socket.
   */
  bool lookupCacheRecord(const std::string& sessionId,
                         AsyncSSLSocket* sslSock);

  /**
   * Restart all clients waiting for the answer to an external cache query
   */
  void restartSSLAccept(const SSLCacheProvider::CacheContext* cacheCtx);

  /**
   * Get or create the LRU cache for the given VIP ID
   */
  static std::shared_ptr<ShardedLocalSSLSessionCache> getLocalCache(
    uint32_t maxCacheSize, uint32_t cacheCullSize);

  /**
   * static functions registered as callbacks to openssl via
   * SSL_CTX_sess_set_new/get/remove_cb
   */
  static int newSessionCallback(SSL* ssl, SSL_SESSION* session);
  static void removeSessionCallback(SSL_CTX* ctx, SSL_SESSION* session);
  static SSL_SESSION* getSessionCallback(SSL* ssl, unsigned char* session_id,
                                         int id_len, int* copyflag);

  static int32_t sExDataIndex_;
  static std::shared_ptr<ShardedLocalSSLSessionCache> sCache_;
  static std::mutex sCacheLock_;
};

}
