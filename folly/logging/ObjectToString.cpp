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

#include <folly/logging/ObjectToString.h>

#include <folly/Demangle.h>

namespace {

void appendHexdump(std::string& str, const uint8_t* data, size_t length) {
  static constexpr const char nibbleToChar[] = "0123456789abcdef";
  for (size_t index = 0; index < length; ++index) {
    str.push_back(' ');
    str.push_back(nibbleToChar[(data[index] >> 4) & 0xf]);
    str.push_back(nibbleToChar[data[index] & 0xf]);
  }
}

} // namespace

namespace folly {
namespace logging {
namespace detail {

void appendRawObjectInfo(
    std::string& result,
    const std::type_info* type,
    const uint8_t* data,
    size_t length) {
  if (type) {
    result.push_back('[');
    try {
      toAppend(folly::demangle(*type), &result);
    } catch (const std::exception&) {
      result.append("unknown_type");
    }
    result.append(" of size ");
  } else {
    result.append("[object of size ");
  }
  toAppend(length, &result);
  result.append(":");
  appendHexdump(result, data, length);
  result.push_back(']');
}

} // namespace detail
} // namespace logging
} // namespace folly
