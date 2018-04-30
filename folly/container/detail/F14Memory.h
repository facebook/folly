/*
 * Copyright 2017-present Facebook, Inc.
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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include <folly/Portability.h>
#include <folly/lang/SafeAssert.h>

namespace folly {
namespace f14 {
namespace detail {

template <typename Ptr>
using NonConstPtr = typename std::pointer_traits<Ptr>::template rebind<
    std::remove_const_t<typename std::pointer_traits<Ptr>::element_type>>;

//////// TaggedPtr

template <typename Ptr>
class TaggedPtr {
 public:
  TaggedPtr() = default;
  TaggedPtr(TaggedPtr const&) = default;
  TaggedPtr(TaggedPtr&&) = default;
  TaggedPtr& operator=(TaggedPtr const&) = default;
  TaggedPtr& operator=(TaggedPtr&&) = default;

  TaggedPtr(Ptr p, uint8_t e) noexcept : ptr_{p}, extra_{e} {}

  /* implicit */ TaggedPtr(std::nullptr_t) noexcept {}

  TaggedPtr& operator=(std::nullptr_t) noexcept {
    ptr_ = nullptr;
    extra_ = 0;
    return *this;
  }

  typename std::pointer_traits<Ptr>::element_type& operator*() const noexcept {
    return *ptr_;
  }

  typename std::pointer_traits<Ptr>::element_type* operator->() const noexcept {
    return std::addressof(*ptr_);
  }

  Ptr ptr() const {
    return ptr_;
  }

  void setPtr(Ptr p) {
    ptr_ = p;
  }

  uint8_t extra() const {
    return extra_;
  }

  void setExtra(uint8_t e) {
    extra_ = e;
  }

  bool operator==(TaggedPtr const& rhs) const noexcept {
    return ptr_ == rhs.ptr_ && extra_ == rhs.extra_;
  }
  bool operator!=(TaggedPtr const& rhs) const noexcept {
    return !(*this == rhs);
  }

  bool operator<(TaggedPtr const& rhs) const noexcept {
    return ptr_ != rhs.ptr_ ? ptr_ < rhs.ptr_ : extra_ < rhs.extra_;
  }

  bool operator==(std::nullptr_t) const noexcept {
    return ptr_ == nullptr;
  }
  bool operator!=(std::nullptr_t) const noexcept {
    return !(*this == nullptr);
  }

 private:
  Ptr ptr_{};
  uint8_t extra_{};
};

#if FOLLY_X64 || FOLLY_AARCH64

template <typename T>
class TaggedPtr<T*> {
 public:
  TaggedPtr() = default;
  TaggedPtr(TaggedPtr const&) = default;
  TaggedPtr(TaggedPtr&&) = default;
  TaggedPtr& operator=(TaggedPtr const&) = default;
  TaggedPtr& operator=(TaggedPtr&&) = default;

  TaggedPtr(T* p, uint8_t e) noexcept
      : raw_{(reinterpret_cast<uintptr_t>(p) << 8) | e} {
    FOLLY_SAFE_DCHECK(ptr() == p, "");
  }

  /* implicit */ TaggedPtr(std::nullptr_t) noexcept : raw_{0} {}

  TaggedPtr& operator=(std::nullptr_t) noexcept {
    raw_ = 0;
    return *this;
  }

  T& operator*() const noexcept {
    return *ptr();
  }

  T* operator->() const noexcept {
    return std::addressof(*ptr());
  }

  T* ptr() const {
    return reinterpret_cast<T*>(raw_ >> 8);
  }

  void setPtr(T* p) {
    *this = TaggedPtr{p, extra()};
    FOLLY_SAFE_DCHECK(ptr() == p, "");
  }

  uint8_t extra() const {
    return static_cast<uint8_t>(raw_);
  }

  void setExtra(uint8_t e) {
    *this = TaggedPtr{ptr(), e};
  }

  bool operator==(TaggedPtr const& rhs) const {
    return raw_ == rhs.raw_;
  }
  bool operator!=(TaggedPtr const& rhs) const {
    return !(*this == rhs);
  }

  bool operator<(TaggedPtr const& rhs) const noexcept {
    return raw_ < rhs.raw_;
  }

  bool operator==(std::nullptr_t) const noexcept {
    return raw_ == 0;
  }
  bool operator!=(std::nullptr_t) const noexcept {
    return !(*this == nullptr);
  }

 private:
  // TODO: verify no high-bit extension needed on aarch64
  uintptr_t raw_;
};

#endif // FOLLY_X64 || FOLLY_AARCH64

} // namespace detail
} // namespace f14
} // namespace folly
