/*
 * Copyright 2014 Facebook, Inc.
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

#include <folly/experimental/wangle/ManagedConnection.h>

#include <folly/experimental/wangle/ConnectionManager.h>

namespace folly { namespace wangle {

ManagedConnection::ManagedConnection()
  : connectionManager_(nullptr) {
}

ManagedConnection::~ManagedConnection() {
  if (connectionManager_) {
    connectionManager_->removeConnection(this);
  }
}

void
ManagedConnection::resetTimeout() {
  if (connectionManager_) {
    connectionManager_->scheduleTimeout(this);
  }
}

void
ManagedConnection::scheduleTimeout(
  folly::HHWheelTimer::Callback* callback,
    std::chrono::milliseconds timeout) {
  if (connectionManager_) {
    connectionManager_->scheduleTimeout(callback, timeout);
  }
}

////////////////////// Globals /////////////////////

std::ostream&
operator<<(std::ostream& os, const ManagedConnection& conn) {
  conn.describe(os);
  return os;
}

}} // folly::wangle
