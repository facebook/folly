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
#include <folly/experimental/logging/Logger.h>

#include <ostream>

#include <folly/Conv.h>
#include <folly/experimental/logging/LogMessage.h>
#include <folly/experimental/logging/LoggerDB.h>

using std::string;

namespace folly {

Logger::Logger(StringPiece name) : Logger{LoggerDB::get()->getCategory(name)} {}

Logger::Logger(LogCategory* cat) : category_(cat) {}

Logger::Logger(LoggerDB* db, StringPiece name)
    : Logger{db->getCategory(name)} {}
}
