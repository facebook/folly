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

#include <folly/settings/Types.h>

namespace folly {
namespace settings {

std::string_view toString(SetErrorCode code) {
  switch (code) {
    case SetErrorCode::NotFound:
      return "not found";
    case SetErrorCode::Rejected:
      return "rejected";
    case SetErrorCode::FrozenImmutable:
      return "frozen immutable";
  }
  // no default case in the switch above! to force the compiler to warn
  return "<unknown>"; // this is not a switch default case
}

} // namespace settings
} // namespace folly
