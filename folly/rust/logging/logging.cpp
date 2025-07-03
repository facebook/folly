/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <folly/rust/logging/logging.h>

#include <folly/Range.h>
#include <folly/logging/LogConfigParser.h>
#include <folly/logging/LoggerDB.h>

namespace facebook::rust {

void updateLoggingConfig(rust::Str str) {
  folly::StringPiece config = {str.data(), str.length()};
  folly::LogConfig logConfig = folly::parseLogConfig(config);
  folly::LoggerDB::get().updateConfig(logConfig);
}

} // namespace facebook::rust
