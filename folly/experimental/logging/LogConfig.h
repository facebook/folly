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

#include <folly/experimental/logging/LogCategoryConfig.h>
#include <folly/experimental/logging/LogHandlerConfig.h>

namespace folly {

/**
 * LogConfig contains configuration for the LoggerDB.
 *
 * This includes information about the log levels for log categories,
 * as well as what log handlers are configured and which categories they are
 * attached to.
 */
class LogConfig {
 public:
  using CategoryConfigMap = std::unordered_map<std::string, LogCategoryConfig>;
  using HandlerConfigMap = std::unordered_map<std::string, LogHandlerConfig>;

  LogConfig() = default;
  explicit LogConfig(
      HandlerConfigMap handlerConfigs,
      CategoryConfigMap catConfigs)
      : handlerConfigs_{std::move(handlerConfigs)},
        categoryConfigs_{std::move(catConfigs)} {}

  const CategoryConfigMap& getCategoryConfigs() const {
    return categoryConfigs_;
  }
  const HandlerConfigMap& getHandlerConfigs() const {
    return handlerConfigs_;
  }

  bool operator==(const LogConfig& other) const;
  bool operator!=(const LogConfig& other) const;

 private:
  HandlerConfigMap handlerConfigs_;
  CategoryConfigMap categoryConfigs_;
};

} // namespace folly
