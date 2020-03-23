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

#include <folly/logging/BridgeFromGoogleLogging.h>

#include <folly/Utility.h>
#include <folly/logging/Logger.h>
#include <folly/logging/xlog.h>

namespace folly {
namespace logging {

namespace {
constexpr folly::LogLevel asFollyLogLevel(::google::LogSeverity severity) {
  switch (severity) {
    case ::google::GLOG_FATAL:
      return folly::LogLevel::FATAL;
    case ::google::GLOG_ERROR:
      return folly::LogLevel::ERR;
    case ::google::GLOG_WARNING:
      return folly::LogLevel::WARNING;
    case ::google::GLOG_INFO:
      return folly::LogLevel::INFO;
    default:
      return folly::LogLevel::INFO2;
  }
}
} // namespace

BridgeFromGoogleLogging::BridgeFromGoogleLogging() {
  ::google::AddLogSink(this);
}

BridgeFromGoogleLogging::~BridgeFromGoogleLogging() noexcept {
  ::google::RemoveLogSink(this);
}

void BridgeFromGoogleLogging::send(
    ::google::LogSeverity severity,
    const char* full_filename,
    const char* base_filename,
    int line,
    const struct ::tm* pTime,
    const char* message,
    size_t message_len,
    int32_t usecs) {
  struct ::tm time = *pTime;
  folly::Logger const logger{full_filename};
  auto follyLevel = asFollyLogLevel(severity);
  if (logger.getCategory()->logCheck(follyLevel)) {
    folly::LogMessage logMessage{
        logger.getCategory(),
        follyLevel,
        std::chrono::system_clock::from_time_t(mktime(&time)) +
            std::chrono::microseconds(usecs),
        base_filename,
        static_cast<unsigned>(line),
        {},
        std::string{message, message_len}};
    logger.getCategory()->admitMessage(logMessage);
  }
}

void BridgeFromGoogleLogging::send(
    ::google::LogSeverity severity,
    const char* full_filename,
    const char* base_filename,
    int line,
    const struct ::tm* pTime,
    const char* message,
    size_t message_len) {
  struct ::tm time = *pTime;
  auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now() -
      std::chrono::system_clock::from_time_t(mktime(&time)));
  send(
      severity,
      full_filename,
      base_filename,
      line,
      pTime,
      message,
      message_len,
      folly::to_narrow(usecs.count()));
}

} // namespace logging
} // namespace folly
