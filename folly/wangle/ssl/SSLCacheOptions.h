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

#include <chrono>
#include <cstdint>

namespace folly {

struct SSLCacheOptions {
  std::chrono::seconds sslCacheTimeout;
  uint64_t maxSSLCacheSize;
  uint64_t sslCacheFlushSize;
};

}
