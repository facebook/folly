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

#include <fmt/format.h>
#include <folly/Demangle.h>

namespace folly {
namespace channels {
namespace detail {

/**
 * A PointerVariant stores a pointer of one of two possible types.
 */
template <typename FirstType, typename SecondType>
class PointerVariant {
 public:
  template <typename T>
  explicit PointerVariant(T* pointer) {
    set(pointer);
  }

  PointerVariant(PointerVariant&& other) noexcept
      : storage_(std::exchange(other.storage_, 0)) {}

  PointerVariant& operator=(PointerVariant&& other) noexcept {
    storage_ = std::exchange(other.storage_, 0);
    return *this;
  }

  /**
   * Returns the zero-based index of the type that is currently held.
   */
  size_t index() const { return static_cast<size_t>(storage_ & kTypeMask); }

  /**
   * Returns the pointer stored in the PointerVariant, if the type matches the
   * first type. If the stored type does not match the first type, an exception
   * will be thrown.
   */
  inline FirstType* get(folly::tag_t<FirstType>) const {
    ensureCorrectType(false /* secondType */);
    return reinterpret_cast<FirstType*>(storage_ & kPointerMask);
  }

  /**
   * Returns the pointer stored in the PointerVariant, if the type matches the
   * second type. If the stored type does not match the second type, an
   * exception will be thrown.
   */
  inline SecondType* get(folly::tag_t<SecondType>) const {
    ensureCorrectType(true /* secondType */);
    return reinterpret_cast<SecondType*>(storage_ & kPointerMask);
  }

  /**
   * Store a new pointer of type FirstType in the PointerVariant.
   */
  void set(FirstType* pointer) {
    storage_ = reinterpret_cast<intptr_t>(pointer);
  }

  /**
   * Store a new pointer of type SecondType in the PointerVariant.
   */
  void set(SecondType* pointer) {
    storage_ = reinterpret_cast<intptr_t>(pointer) | kTypeMask;
  }

 private:
  void ensureCorrectType(bool secondType) const {
    if (secondType != !!(storage_ & kTypeMask)) {
      throw std::runtime_error(fmt::format(
          "Incorrect type specified. Given: {}, Stored: {}",
          secondType ? folly::demangle(typeid(SecondType).name())
                     : folly::demangle(typeid(FirstType).name()),
          storage_ & kTypeMask ? folly::demangle(typeid(SecondType).name())
                               : folly::demangle(typeid(FirstType).name())));
    }
  }

  static constexpr intptr_t kTypeMask = 1;
  static constexpr intptr_t kPointerMask = ~kTypeMask;

  intptr_t storage_;
};
} // namespace detail
} // namespace channels
} // namespace folly
