/*
 * Copyright 2017-present Facebook, Inc.
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
#include <folly/experimental/logging/Init.h>

#include <folly/experimental/logging/LogConfig.h>
#include <folly/experimental/logging/LogConfigParser.h>
#include <folly/experimental/logging/LoggerDB.h>
#include <folly/experimental/logging/StreamHandlerFactory.h>

namespace folly {

void initLogging(StringPiece configString) {
  auto* const baseConfigStr = getBaseLoggingConfig();
  // Return early if we have nothing to do
  if (!baseConfigStr && configString.empty()) {
    return;
  }

  // Parse the configuration string(s)
  LogConfig config;
  if (baseConfigStr) {
    config = parseLogConfig(baseConfigStr);
    if (!configString.empty()) {
      config.update(parseLogConfig(configString));
    }
  } else {
    config = parseLogConfig(configString);
  }

  // Apply the config settings
  LoggerDB::get().updateConfig(config);
}

} // namespace folly
