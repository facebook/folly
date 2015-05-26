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

#include <folly/io/async/AsyncSSLSocket.h>

namespace folly {

class SSLSessionCacheManager;

/**
 * Interface to be implemented by providers of external session caches
 */
class SSLCacheProvider {
public:
  /**
   * Context saved during an external cache request that is used to
   * resume the waiting client.
   */
  typedef struct {
    std::string sessionId;
    SSL_SESSION* session;
    SSLSessionCacheManager* manager;
    AsyncSSLSocket* sslSocket;
    std::unique_ptr<
      folly::DelayedDestruction::DestructorGuard> guard;
  } CacheContext;

  virtual ~SSLCacheProvider() {}

  /**
   * Store a session in the external cache.
   * @param sessionId   Identifier that can be used later to fetch the
   *                      session with getAsync()
   * @param value       Serialized session to store
   * @param expiration  Relative expiration time: seconds from now
   * @return true if the storing of the session is initiated successfully
   *         (though not necessarily completed; the completion may
   *         happen either before or after this method returns), or
   *         false if the storing cannot be initiated due to an error.
   */
  virtual bool setAsync(const std::string& sessionId,
                        const std::string& value,
                        std::chrono::seconds expiration) = 0;

  /**
   * Retrieve a session from the external cache. When done, call
   * the cache manager's onGetSuccess() or onGetFailure() callback.
   * @param sessionId   Session ID to fetch
   * @param context     Data to pass back to the SSLSessionCacheManager
   *                      in the completion callback
   * @return true if the lookup of the session is initiated successfully
   *         (though not necessarily completed; the completion may
   *         happen either before or after this method returns), or
   *         false if the lookup cannot be initiated due to an error.
   */
  virtual bool getAsync(const std::string& sessionId,
                        CacheContext* context) = 0;

};

}
