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

#pragma once
#define GLOG_NO_ABBREVIATED_SEVERITIES

#include <cstdint>

#include <glog/logging.h>

namespace folly {
namespace logging {
/* Class to bridge GLOG to folly logging.
 *
 * You only need one instance of this class in your program at a time. While
 * it's in scope it will bridge all GLOG messages to the folly logging
 * pipeline.
 *
 * You probably want this very early in `main` as:
 *   folly::logging::BridgeFromGoogleLogging glogToXlogBridge{};
 */
struct BridgeFromGoogleLogging : ::google::LogSink {
  BridgeFromGoogleLogging();
  ~BridgeFromGoogleLogging() noexcept override;

  void send(
      ::google::LogSeverity severity,
      const char* full_filename,
      const char* base_filename,
      int line,
      const struct ::tm* pTime,
      const char* message,
      size_t message_len,
      int32_t usecs);

  void send(
      ::google::LogSeverity severity,
      const char* full_filename,
      const char* base_filename,
      int line,
      const struct ::tm* pTime,
      const char* message,
      size_t message_len) override;
};
} // namespace logging
} // namespace folly
