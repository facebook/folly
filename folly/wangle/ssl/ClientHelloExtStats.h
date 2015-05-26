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

namespace folly {

class ClientHelloExtStats {
 public:
  virtual ~ClientHelloExtStats() noexcept {}

  // client hello
  virtual void recordAbsentHostname() noexcept = 0;
  virtual void recordMatch() noexcept = 0;
  virtual void recordNotMatch() noexcept = 0;
};

}
