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
 * Before it is applied, the input configuration settings are first combined
 * with some basic defaults on the root log category.  The defaults set the
 * root log level to WARN, and attach a log handler named "default" that writes
 * messages to stderr.  However, these base settings can be overridden if the
 * input string specifies alternate settings for the root log category.
 *
 * Note that it is safe for code to use logging functions before calling
 * initLogging().  However, messages logged before initLogging() is called will
 * be ignored since no log handler objects have been defined.
 */
void initLogging(folly::StringPiece configString = "");

} // namespace folly
