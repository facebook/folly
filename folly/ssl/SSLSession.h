/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#pragma once

namespace folly {
namespace ssl {

/**
 * An abstraction for SSL sessions.
 *
 * SSLSession allows users to store and pass SSL sessions (e.g for
 * resumption) while abstracting away implementation-specific
 * details.
 *
 * SSLSession is intended to be used by deriving a new class
 * which handles the implementation details, and downcasting
 * whenever access is needed to the underlying implementation.
 */

class SSLSession {
 public:
  virtual ~SSLSession() = default;
};

} // namespace ssl
} // namespace folly
