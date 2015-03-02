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

class IConnectionCounter {
 public:
  virtual uint64_t getNumConnections() const = 0;

  /**
   * Get the maximum number of non-whitelisted client-side connections
   * across all Acceptors managed by this. A value
   * of zero means "unlimited."
   */
  virtual uint64_t getMaxConnections() const = 0;

  /**
   * Increment the count of client-side connections.
   */
  virtual void onConnectionAdded() = 0;

  /**
   * Decrement the count of client-side connections.
   */
  virtual void onConnectionRemoved() = 0;
  virtual ~IConnectionCounter() {}
};

class SimpleConnectionCounter: public IConnectionCounter {
 public:
  uint64_t getNumConnections() const override { return numConnections_; }
  uint64_t getMaxConnections() const override { return maxConnections_; }
  void setMaxConnections(uint64_t maxConnections) {
    maxConnections_ = maxConnections;
  }

  void onConnectionAdded() override { numConnections_++; }
  void onConnectionRemoved() override { numConnections_--; }
  virtual ~SimpleConnectionCounter() {}

 protected:
  uint64_t maxConnections_{0};
  uint64_t numConnections_{0};
};

}
