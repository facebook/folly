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

#include <memory>
#include <mutex>
#include <string>
#include <typeindex>

#include <folly/CachelinePadded.h>
#include <folly/Conv.h>
#include <folly/Range.h>
#include <folly/SharedMutex.h>
#include <folly/ThreadLocal.h>

namespace folly {
namespace settings {

struct SettingMetadata;

namespace detail {

template <class Type>
struct SettingContents {
  std::string updateReason;
  Type value;

  template <class... Args>
  SettingContents(std::string _reason, Args&&... args)
      : updateReason(std::move(_reason)), value(std::forward<Args>(args)...) {}
};

class SettingCoreBase {
 public:
  virtual void setFromString(StringPiece newValue, StringPiece reason) = 0;
  virtual std::pair<std::string, std::string> getAsString() const = 0;
  virtual void resetToDefault() = 0;
  virtual const SettingMetadata& meta() const = 0;
  virtual ~SettingCoreBase() {}
};

void registerSetting(SettingCoreBase& core);

template <class Type>
class SettingCore : public SettingCoreBase {
 public:
  using Contents = SettingContents<Type>;

  void setFromString(StringPiece newValue, StringPiece reason) override {
    set(to<Type>(newValue), reason.str());
  }
  std::pair<std::string, std::string> getAsString() const override {
    auto contents = *const_cast<SettingCore*>(this)->tlValue();
    return std::make_pair(
        to<std::string>(contents.value), contents.updateReason);
  }
  void resetToDefault() override {
    set(defaultValue_, "default");
  }
  const SettingMetadata& meta() const override {
    return meta_;
  }

  const Type& get() const {
    return const_cast<SettingCore*>(this)->tlValue()->value;
  }
  void set(const Type& t, StringPiece reason) {
    SharedMutex::WriteHolder lg(globalLock_);
    globalValue_ = std::make_shared<Contents>(reason.str(), t);
    ++(*globalVersion_);
  }

  SettingCore(const SettingMetadata& meta, Type defaultValue)
      : meta_(meta),
        defaultValue_(std::move(defaultValue)),
        globalValue_(std::make_shared<Contents>("default", defaultValue_)),
        localValue_([]() {
          return new CachelinePadded<
              Indestructible<std::pair<size_t, std::shared_ptr<Contents>>>>(
              0, nullptr);
        }) {
    registerSetting(*this);
  }

 private:
  const SettingMetadata& meta_;
  const Type defaultValue_;

  SharedMutex globalLock_;
  std::shared_ptr<Contents> globalValue_;

  /* Local versions start at 0, this will force a read on first local access. */
  CachelinePadded<std::atomic<size_t>> globalVersion_{1};

  ThreadLocal<CachelinePadded<
      Indestructible<std::pair<size_t, std::shared_ptr<Contents>>>>>
      localValue_;

  std::shared_ptr<Contents>& tlValue() {
    auto& value = ***localValue_;
    while (value.first < *globalVersion_) {
      /* If this destroys the old value, do it without holding the lock */
      value.second.reset();
      SharedMutex::ReadHolder lg(globalLock_);
      value.first = *globalVersion_;
      value.second = globalValue_;
    }
    return value.second;
  }
};

} // namespace detail
} // namespace settings
} // namespace folly
