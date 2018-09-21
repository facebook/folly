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
#include <folly/experimental/settings/SettingsMetadata.h>

namespace folly {
namespace settings {
namespace detail {

/**
 * Can we store T in a global atomic?
 */
template <class T>
struct IsSmallPOD
    : std::integral_constant<
          bool,
          std::is_trivial<T>::value && sizeof(T) <= sizeof(uint64_t)> {};

template <class T>
struct SettingContents {
  std::string updateReason;
  T value;

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

template <class T>
std::enable_if_t<std::is_constructible<T, StringPiece>::value, T>
convertOrConstruct(StringPiece newValue) {
  return T(newValue);
}
template <class T>
std::enable_if_t<!std::is_constructible<T, StringPiece>::value, T>
convertOrConstruct(StringPiece newValue) {
  return to<T>(newValue);
}

template <class T>
class SettingCore : public SettingCoreBase {
 public:
  using Contents = SettingContents<T>;

  void setFromString(StringPiece newValue, StringPiece reason) override {
    set(convertOrConstruct<T>(newValue), reason.str());
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

  std::conditional_t<IsSmallPOD<T>::value, T, const T&> get() const {
    return getImpl(IsSmallPOD<T>(), trivialStorage_);
  }
  const T& getSlow() const {
    return getImpl(std::false_type{}, trivialStorage_);
  }
  /***
   * SmallPOD version: just read the global atomic
   */
  T getImpl(std::true_type, std::atomic<uint64_t>& trivialStorage) const {
    uint64_t v = trivialStorage.load();
    T t;
    std::memcpy(&t, &v, sizeof(T));
    return t;
  }

  /**
   * Non-SmallPOD version: read the thread local shared_ptr
   */
  const T& getImpl(std::false_type, std::atomic<uint64_t>& /* ignored */)
      const {
    return const_cast<SettingCore*>(this)->tlValue()->value;
  }

  void set(const T& t, StringPiece reason) {
    SharedMutex::WriteHolder lg(globalLock_);
    globalValue_ = std::make_shared<Contents>(reason.str(), t);
    if (IsSmallPOD<T>::value) {
      uint64_t v = 0;
      std::memcpy(&v, &t, sizeof(T));
      trivialStorage_.store(v);
    }
    ++(*globalVersion_);
  }

  SettingCore(
      SettingMetadata meta,
      T defaultValue,
      std::atomic<uint64_t>& trivialStorage)
      : meta_(std::move(meta)),
        defaultValue_(std::move(defaultValue)),
        trivialStorage_(trivialStorage),
        localValue_([]() {
          return new CachelinePadded<
              Indestructible<std::pair<size_t, std::shared_ptr<Contents>>>>(
              0, nullptr);
        }) {
    set(defaultValue_, "default");
    registerSetting(*this);
  }

 private:
  SettingMetadata meta_;
  const T defaultValue_;

  SharedMutex globalLock_;
  std::shared_ptr<Contents> globalValue_;

  std::atomic<uint64_t>& trivialStorage_;

  /* Local versions start at 0, this will force a read on first local access. */
  CachelinePadded<std::atomic<size_t>> globalVersion_{1};

  ThreadLocal<CachelinePadded<
      Indestructible<std::pair<size_t, std::shared_ptr<Contents>>>>>
      localValue_;

  FOLLY_ALWAYS_INLINE std::shared_ptr<Contents>& tlValue() {
    auto& value = ***localValue_;
    if (LIKELY(value.first == *globalVersion_)) {
      return value.second;
    }
    return tlValueSlow();
  }
  FOLLY_NOINLINE std::shared_ptr<Contents>& tlValueSlow() {
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
