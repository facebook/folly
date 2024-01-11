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

#pragma once

#include <string_view>

#include <folly/Expected.h>
#include <folly/Range.h>
#include <folly/Unit.h>

namespace folly {
namespace settings {

enum class SetErrorCode {
  NotFound,
  Rejected,
  FrozenImmutable,
};

using SetResult = Expected<Unit, SetErrorCode>;

std::string_view toString(SetErrorCode code);

enum class Mutability {
  Mutable,
  Immutable,
};

/**
 * Static information about the setting definition
 */
struct SettingMetadata {
  /**
   * Project string.
   */
  StringPiece project;

  /**
   * Setting name within the project.
   */
  StringPiece name;

  /**
   * String representation of the type.
   */
  StringPiece typeStr;

  /**
   * typeid() of the type.
   */
  const std::type_info& typeId;

  /**
   * String representation of the default value.
   * (note: string literal default values will be stringified with quotes)
   */
  StringPiece defaultStr;

  /**
   * Determines if the setting can change after initialization.
   */
  Mutability mutability;

  /**
   * Setting description field.
   */
  StringPiece description;
};

} // namespace settings
} // namespace folly
