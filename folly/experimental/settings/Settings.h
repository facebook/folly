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

#include <folly/Indestructible.h>
#include <folly/Range.h>
#include <folly/experimental/settings/detail/SettingsImpl.h>

namespace folly {
namespace settings {
namespace detail {

template <class SettingMeta>
class SettingHandle {
 public:
  /**
   * Setting type
   */
  using Type = typename SettingMeta::Type;

  /**
   * Returns the setting's current value.
   * Note that the returned reference is not guaranteed to be long-lived
   * and should not be saved anywhere.
   */
  const Type& operator*() const {
    return core().get();
  }
  const Type& operator->() const {
    return core().get();
  }

  /**
   * Atomically updates the setting's current value.
   * @param reason  Will be stored with the current value, useful for debugging.
   */
  static void set(const Type& t, folly::StringPiece reason = "api") {
    core().set(t, reason);
  }

  SettingHandle() {
    /* Ensure setting is registered */
    core();
  }

 private:
  static SettingCore<SettingMeta>& core() {
    static /* library-local */
        Indestructible<std::shared_ptr<SettingCoreBase>>
            core = registerImpl(
                SettingMeta::project(),
                SettingMeta::name(),
                typeid(typename SettingMeta::Type),
                SettingMeta::defaultString(),
                SettingMeta::desc(),
                SettingMeta::unique(),
                std::make_shared<SettingCore<SettingMeta>>());

    return static_cast<SettingCore<SettingMeta>&>(**core);
  }
};

} // namespace detail

#define FOLLY_SETTING_IMPL(_project, _Type, _name, _def, _desc, _unique) \
  namespace {                                                            \
  struct FOLLY_SETTINGS_META__##_project##_##_name {                     \
    using Type = _Type;                                                  \
    static folly::StringPiece project() {                                \
      return #_project;                                                  \
    }                                                                    \
    static folly::StringPiece name() {                                   \
      return #_name;                                                     \
    }                                                                    \
    static Type def() {                                                  \
      return _def;                                                       \
    }                                                                    \
    static folly::StringPiece defaultString() {                          \
      return #_def;                                                      \
    }                                                                    \
    static folly::StringPiece desc() {                                   \
      return _desc;                                                      \
    }                                                                    \
    static bool unique() {                                               \
      return _unique;                                                    \
    }                                                                    \
  };                                                                     \
  folly::settings::detail::SettingHandle<                                \
      FOLLY_SETTINGS_META__##_project##_##_name>                         \
      SETTING_##_project##_##_name;                                      \
  }                                                                      \
  /* hack to require a trailing semicolon */                             \
  int FOLLY_SETTINGS_IGNORE__##_project##_##_name()

/**
 * Defines a setting.
 *
 * FOLLY_SETTING_SHARED(): syntactically, think of it like a class
 * definition.  You can place the identical setting definitions in
 * distinct translation units, but not in the same translation unit.
 * In particular, you can place a setting definition in a header file
 * and include it from multiple .cpp files - all of these definitions
 * will refer to a single setting.
 *
 * FOLLY_SETTING() variant can only be placed in a single translation unit
 * and will be checked against accidental collisions.
 *
 * The setting API can be accessed via SETTING_project_name::<api_func>() and
 * is documented in the SettingHandle class.
 *
 * While the specific SETTING_project_name classes are declared
 * inplace and are namespace local, all settings for a given project
 * share a common namespace and collisions are verified at runtime on
 * program startup.
 *
 * @param _project  Project identifier, can only contain [a-zA-Z0-9]
 * @param _Type  setting value type
 * @param _name  setting name within the project, can only contain [_a-zA-Z0-9].
 *   The string "<project>_<name>" must be unique for the whole program.
 * @param _def   default value for the setting
 * @param _desc  setting documentation
 */
#define FOLLY_SETTING(_project, _Type, _name, _def, _desc) \
  FOLLY_SETTING_IMPL(_project, _Type, _name, _def, _desc, /* unique */ true)
#define FOLLY_SETTING_SHARED(_project, _Type, _name, _def, _desc) \
  FOLLY_SETTING_IMPL(_project, _Type, _name, _def, _desc, /* unique */ false)

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
 * @return If the setting exists, the current (to<string>(value),
 *   reason) pair.  Empty Optional otherwise.
 */
folly::Optional<std::pair<std::string, std::string>> getAsString(
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
 * func(name, to<string>(value), reason, typeid(Type)) for each.
 */
void forEachSetting(const std::function<void(
                        folly::StringPiece,
                        folly::StringPiece,
                        folly::StringPiece,
                        const std::type_info&)>& func);

} // namespace settings
} // namespace folly
