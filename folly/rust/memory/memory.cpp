/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <folly/rust/memory/memory.h>

#include <string>

#include <folly/memory/MallctlHelper.h>

using rust::Str;

namespace facebook::rust {

void mallctlWriteBool(Str cmd, bool value) {
  std::string cmdStr(cmd);
  folly::mallctlWrite(cmdStr.data(), value);
}

void mallctlWriteString(Str cmd, Str value) {
  std::string cmdStr(cmd);
  std::string valueStr(value);
  folly::mallctlWrite(cmdStr.data(), valueStr.data());
}

void mallctlReadBool(Str cmd, bool& value) {
  std::string cmdStr(cmd);
  folly::mallctlRead(cmdStr.data(), &value);
}

void mallctlCall(Str cmd) {
  std::string cmdStr(cmd);
  folly::mallctlCall(cmdStr.data());
}

} // namespace facebook::rust
