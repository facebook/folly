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
#include <folly/experimental/logging/LogStreamProcessor.h>

#include <cassert>

#include <folly/experimental/logging/LogStream.h>

namespace folly {
void LogStreamProcessor::operator&(std::ostream& stream) noexcept {
  // Note that processMessage() is not noexcept and theoretically may throw.
  // However, the only exception that should be possible is std::bad_alloc if
  // we fail to allocate memory.  We intentionally let our noexcept specifier
  // crash in that case, since the program likely won't be able to continue
  // anyway.
  //
  // Any other error here is unexpected and we also want to fail hard
  // in that situation too.
  auto& logStream = static_cast<LogStream&>(stream);
  category_->processMessage(LogMessage{category_,
                                       level_,
                                       filename_,
                                       lineNumber_,
                                       extractMessageString(logStream)});
}

void LogStreamProcessor::operator&(LogStream&& stream) noexcept {
  // This version of operator&() is generally only invoked when
  // no streaming arguments were supplied to the logging macro.
  // Therefore we don't bother calling extractMessageString(stream),
  // and just directly use message_.
  assert(stream.empty());

  category_->processMessage(LogMessage{
      category_, level_, filename_, lineNumber_, std::move(message_)});
}

std::string LogStreamProcessor::extractMessageString(
    LogStream& stream) noexcept {
  if (stream.empty()) {
    return std::move(message_);
  }

  if (message_.empty()) {
    return stream.extractString();
  }
  message_.append(stream.extractString());
  return std::move(message_);
}
}
