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

#include <folly/logging/xlog.h>

class LogOnDestruction {
 public:
  // The constructor does not copy the message simply so we can
  // keep it constexpr
  constexpr LogOnDestruction(const char* msg) : msg_{msg} {}
  ~LogOnDestruction() {
    XLOGF(INFO, "LogOnDestruction({}) destroying", msg_);
  }

 private:
  const char* msg_{nullptr};
};

class LogOnConstruction {
 public:
  template <typename... Args>
  LogOnConstruction(folly::StringPiece fmt, Args&&... args) {
    XLOGF(INFO, fmt, std::forward<Args>(args)...);
  }
};
