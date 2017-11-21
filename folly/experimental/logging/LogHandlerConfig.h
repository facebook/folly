/*
 * Copyright 2004-present Facebook, Inc.
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

#include <string>
#include <unordered_map>

#include <folly/Range.h>

namespace folly {

/**
 * Configuration for a LogHandler
 */
class LogHandlerConfig {
 public:
  using Options = std::unordered_map<std::string, std::string>;

  explicit LogHandlerConfig(folly::StringPiece type);
  LogHandlerConfig(folly::StringPiece type, Options options);

  bool operator==(const LogHandlerConfig& other) const;
  bool operator!=(const LogHandlerConfig& other) const;

  std::string type;
  Options options;
};

} // namespace folly
