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
#pragma once

/*
 * This file contains function to help configure the logging library behavior
 * during program start-up.
 */

#include <folly/Range.h>

namespace folly {

/**
 * Initialize the logging library.
 *
 * The input string will be parsed with parseLogConfig() and then applied to
 * the main LoggerDB singleton.
 *
 * This will apply the configuration using LoggerDB::updateConfig(), so the new
 * configuration will be merged into the existing initial configuration defined
 * by initializeLoggerDB().
 *
 * Callers that do want to completely replace the settings can call
 * LoggerDB::resetConfig() instead of using initLogging().
 */
void initLogging(folly::StringPiece configString = "");

} // namespace folly
