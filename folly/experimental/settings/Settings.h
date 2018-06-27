/*
 * Copyright 2018-present Facebook, Inc.
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

#include <functional>
#include <string>

#include <folly/Range.h>
#include <folly/experimental/settings/detail/SettingsImpl.h>

namespace folly {
namespace settings {

/**
 * Static information about the setting definition
 */
struct SettingMetadata {
  /**
   * Project string.
   */
  folly::StringPiece project;

  /**
   * Setting name within the project.
   */
  folly::StringPiece name;

  /**
   * String representation of the type.
   */
  folly::StringPiece typeStr;

  /**
   * typeid() of the type.
   */
  const std::type_info& typeId;

  /**
   * String representation of the default value.
   * (note: string literal default values will be stringified with quotes)
   */
  folly::StringPiece defaultStr;

  /**
   * Setting description field.
   */
  folly::StringPiece description;
};

namespace detail {

template <class Type>
class Setting {
 public:
  /**
   * Returns the setting's current value.  Note that the returned
   * reference is not guaranteed to be long-lived and should not be
   * saved anywhere. In particular, a set() call might invalidate a
   * reference obtained here after some amount of time (on the order
   * of minutes).
   */
  const Type& operator*() const {
    return core_.get();
  }
  const Type* operator->() const {
    return &core_.get();
  }

  /**
   * Atomically updates the setting's current value.  Will invalidate
   * any previous calls to operator*() after some amount of time (on
   * the order of minutes).
   *
   * @param reason  Will be stored with the current value, useful for debugging.
   * @throws std::runtime_error  If we can't convert t to string.
   */
  void set(const Type& t, StringPiece reason = "api") {
    /* Check that we can still display it */
    folly::to<std::string>(t);
    core_.set(t, reason);
  }

  Setting(SettingMetadata meta, Type defaultValue)
      : meta_(std::move(meta)), core_(meta_, std::move(defaultValue)) {}

 private:
  SettingMetadata meta_;
  SettingCore<Type> core_;
};

/* C++20 has std::type_indentity */
template <class T>
struct TypeIdentity {
  using type = T;
};
template <class T>
using TypeIdentityT = typename TypeIdentity<T>::type;

} // namespace detail

/**
 * Defines a setting.
 *
 * FOLLY_SETTING_DEFINE() can only be placed in a single translation unit
 * and will be checked against accidental collisions.
 *
 * The setting API can be accessed via FOLLY_SETTING(project, name).<api_func>()
 * and is documented in the Setting class.
 *
 * All settings for a common namespace; (project, name) must be unique
 * for the whole program.  Collisions are verified at runtime on
 * program startup.
 *
 * @param _project  Project identifier, can only contain [a-zA-Z0-9]
 * @param _name  setting name within the project, can only contain [_a-zA-Z0-9].
 *   The string "<project>_<name>" must be unique for the whole program.
 * @param _Type  setting value type
 * @param _def   default value for the setting
 * @param _desc  setting documentation
 */
#define FOLLY_SETTING_DEFINE(_project, _name, _Type, _def, _desc)         \
  /* Meyers singleton to avoid SIOF */                                    \
  folly::settings::detail::Setting<_Type>&                                \
      FOLLY_SETTINGS_FUNC__##_project##_##_name() {                       \
    static folly::Indestructible<folly::settings::detail::Setting<_Type>> \
        setting(                                                          \
            folly::settings::SettingMetadata{                             \
                #_project, #_name, #_Type, typeid(_Type), #_def, _desc},  \
            folly::settings::detail::TypeIdentityT<_Type>{_def});         \
    return *setting;                                                      \
  }                                                                       \
  /* Ensure the setting is registered even if not used in program */      \
  auto& FOLLY_SETTINGS_INIT__##_project##_##_name =                       \
      FOLLY_SETTINGS_FUNC__##_project##_##_name()

/**
 * Declares a setting that's defined elsewhere.
 */
#define FOLLY_SETTING_DECLARE(_project, _name, _Type) \
  folly::settings::detail::Setting<_Type>&            \
      FOLLY_SETTINGS_FUNC__##_project##_##_name()

/**
 * Accesses a defined setting.
 * Rationale for the macro:
 *  1) Searchability, all settings access is done via FOLLY_SETTING(...)
 *  2) Prevents omitting trailing () by accident, which could
 *     lead to bugs like `auto value = *FOLLY_SETTING_project_name;`,
 *     which compiles but dereferences the function pointer instead of
 *     the setting itself.
 */
#define FOLLY_SETTING(_project, _name) \
  FOLLY_SETTINGS_FUNC__##_project##_##_name()

/**
 * Look up a setting by name, and update the value from a string representation.
 *
 * @returns True if the setting was successfully updated, false if no setting
 *   with that name was found.
 * @throws std::runtime_error  If there's a conversion error.
 */
bool setFromString(
    folly::StringPiece settingName,
    folly::StringPiece newValue,
    folly::StringPiece reason);

/**
 * Type that encapsulates the current pair of (to<string>(value), reason)
 */
using SettingsInfo = std::pair<std::string, std::string>;
/**
 * @return If the setting exists, the current setting information.
 *         Empty Optional otherwise.
 */
folly::Optional<SettingsInfo> getAsString(folly::StringPiece settingName);

/**
 * @return If the setting exists, returns the current settings metadata.
 *         Empty Optional otherwise.
 */
folly::Optional<SettingMetadata> getSettingsMeta(
    folly::StringPiece settingName);

/**
 * Reset the value of the setting identified by name to its default value.
 * The reason will be set to "default".
 *
 * @return  True if the setting was reset, false if the setting is not found.
 */
bool resetToDefault(folly::StringPiece settingName);

/**
 * Iterates over all known settings and calls
 * func(meta, to<string>(value), reason) for each.
 */
void forEachSetting(
    const std::function<
        void(const SettingMetadata&, folly::StringPiece, folly::StringPiece)>&
        func);

} // namespace settings
} // namespace folly
