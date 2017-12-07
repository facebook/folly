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
#include <folly/experimental/logging/Init.h>

#include <folly/experimental/logging/LogConfig.h>
#include <folly/experimental/logging/LogConfigParser.h>
#include <folly/experimental/logging/LoggerDB.h>
#include <folly/experimental/logging/StreamHandlerFactory.h>

namespace folly {

/**
 * The base logging settings to be applied in initLogging(),
 * before any user-specified settings.
 *
 * This defines a log handler named "default" that writes to stderr,
 * and configures the root log category to log to this handler and have a log
 * level setting of WARN.
 *
 * Note that the default log handler uses async=false, so that log messages are
 * written immediately to stderr from the thread that generated the log
 * message.  This is often undesirable, as it can slow down normal processing
 * waiting for logging I/O.  However, using async=true has some trade-offs of
 * its own: it causes a new thread to be started, and not all message may get
 * flushed to stderr if the program crashes.  For now, using async=false seems
 * like the safer trade-off here, but many programs may wish to change this
 * default.
 *
 * The user-specified config string may override any of these values.
 * If the user-specified config string ends up not using the default log
 * handler, the handler will be automatically forgotten by the LoggerDB code.
 */
constexpr StringPiece kDefaultLoggingConfig =
    ".=WARN:default; default=stream:stream=stderr,async=false";

void initLogging(StringPiece configString) {
  // Register the StreamHandlerFactory
  LoggerDB::get()->registerHandlerFactory(
      std::make_unique<StreamHandlerFactory>());

  // TODO: In the future it would be nice to build a better mechanism so that
  // additional LogHandlerFactory objects could be automatically registered on
  // startup if they are linked into our current executable.
  //
  // For now we register only the StreamHandlerFactory.  There is a
  // FileHandlerFactory, but we do not register it by default: it allows
  // appending to arbitrary files based on the config settings, and we do not
  // want to allow this by default for security reasons.  (In the future
  // maybe it would be worth enabling the FileHandlerFactory by default if we
  // confirm that we are not a setuid or setgid binary.  i.e., if the real
  // UID+GID is the same as the effective UID+GID.)

  // Parse the default log level settings before processing the input config
  // string.
  auto config = parseLogConfig(kDefaultLoggingConfig);

  // Then apply the configuration string
  if (!configString.empty()) {
    auto inputConfig = parseLogConfig(configString);
    config.update(inputConfig);
  }

  // Now apply the configuration to the LoggerDB
  LoggerDB::get()->updateConfig(config);
}

} // namespace folly
