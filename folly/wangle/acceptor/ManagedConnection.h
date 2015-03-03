/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/IntrusiveList.h>
#include <ostream>
#include <folly/io/async/HHWheelTimer.h>
#include <folly/io/async/DelayedDestruction.h>

namespace folly { namespace wangle {

class ConnectionManager;

/**
 * Interface describing a connection that can be managed by a
 * container such as an Acceptor.
 */
class ManagedConnection:
    public folly::HHWheelTimer::Callback,
    public folly::DelayedDestruction {
 public:

  ManagedConnection();

  // HHWheelTimer::Callback API (left for subclasses to implement).
  virtual void timeoutExpired() noexcept = 0;

  /**
   * Print a human-readable description of the connection.
   * @param os Destination stream.
   */
  virtual void describe(std::ostream& os) const = 0;

  /**
   * Check whether the connection has any requests outstanding.
   */
  virtual bool isBusy() const = 0;

  /**
   * Notify the connection that a shutdown is pending. This method will be
   * called at the beginning of graceful shutdown.
   */
  virtual void notifyPendingShutdown() = 0;

  /**
   * Instruct the connection that it should shutdown as soon as it is
   * safe. This is called after notifyPendingShutdown().
   */
  virtual void closeWhenIdle() = 0;

  /**
   * Forcibly drop a connection.
   *
   * If a request is in progress, this should cause the connection to be
   * closed with a reset.
   */
  virtual void dropConnection() = 0;

  /**
   * Dump the state of the connection to the log
   */
  virtual void dumpConnectionState(uint8_t loglevel) = 0;

  /**
   * If the connection has a connection manager, reset the timeout countdown to
   * connection manager's default timeout.
   * @note If the connection manager doesn't have the connection scheduled
   *       for a timeout already, this method will schedule one.  If the
   *       connection manager does have the connection connection scheduled
   *       for a timeout, this method will push back the timeout to N msec
   *       from now, where N is the connection manager's timer interval.
   */
  virtual void resetTimeout();

  /**
   * If the connection has a connection manager, reset the timeout countdown to
   * user specified timeout.
   */
  void resetTimeoutTo(std::chrono::milliseconds);

  // Schedule an arbitrary timeout on the HHWheelTimer
  virtual void scheduleTimeout(
    folly::HHWheelTimer::Callback* callback,
    std::chrono::milliseconds timeout);

  ConnectionManager* getConnectionManager() {
    return connectionManager_;
  }

 protected:
  virtual ~ManagedConnection();

 private:
  friend class ConnectionManager;

  void setConnectionManager(ConnectionManager* mgr) {
    connectionManager_ = mgr;
  }

  ConnectionManager* connectionManager_;

  folly::SafeIntrusiveListHook listHook_;
};

std::ostream& operator<<(std::ostream& os, const ManagedConnection& conn);

}} // folly::wangle
