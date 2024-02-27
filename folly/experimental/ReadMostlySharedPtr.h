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

#include <atomic>

#include <folly/Function.h>
#include <folly/experimental/TLRefCount.h>

namespace folly {

template <typename T, typename RefCount>
class ReadMostlyMainPtr;
template <typename T, typename RefCount>
class ReadMostlyWeakPtr;
template <typename T, typename RefCount>
class ReadMostlySharedPtr;
template <typename RefCount>
class ReadMostlyMainPtrDeleter;

using DefaultRefCount = TLRefCount;

namespace detail {

template <typename RefCount = DefaultRefCount>
class ReadMostlySharedPtrCore {
 public:
  std::shared_ptr<const void> getShared() { return ptr_; }

  bool incref() { return ++count_ > 0; }

  void decref() {
    if (--count_ == 0) {
      ptr_.reset();

      decrefWeak();
    }
  }

  void increfWeak() {
    auto value = ++weakCount_;
    DCHECK_GT(value, 0);
  }

  void decrefWeak() {
    if (--weakCount_ == 0) {
      delete this;
    }
  }

  size_t useCount() const { return *count_; }

  ~ReadMostlySharedPtrCore() noexcept {
    assert(*count_ == 0);
    assert(*weakCount_ == 0);
  }

 private:
  template <typename T, typename RefCount2>
  friend class folly::ReadMostlyMainPtr;
  friend class ReadMostlyMainPtrDeleter<RefCount>;

  explicit ReadMostlySharedPtrCore(std::shared_ptr<const void> ptr)
      : ptr_(std::move(ptr)) {}

  RefCount count_;
  RefCount weakCount_;
  std::shared_ptr<const void> ptr_;
};

} // namespace detail

template <typename T, typename RefCount = DefaultRefCount>
class ReadMostlyMainPtr {
 public:
  ReadMostlyMainPtr() {}

  explicit ReadMostlyMainPtr(std::shared_ptr<T> ptr) { reset(std::move(ptr)); }

  ReadMostlyMainPtr(const ReadMostlyMainPtr&) = delete;
  ReadMostlyMainPtr& operator=(const ReadMostlyMainPtr&) = delete;

  ReadMostlyMainPtr(ReadMostlyMainPtr&& other) noexcept {
    *this = std::move(other);
  }

  ReadMostlyMainPtr& operator=(ReadMostlyMainPtr&& other) noexcept {
    std::swap(impl_, other.impl_);
    std::swap(ptrRaw_, other.ptrRaw_);
    return *this;
  }

  bool operator==(const ReadMostlyMainPtr<T, RefCount>& other) const {
    return get() == other.get();
  }

  bool operator==(T* other) const { return get() == other; }

  bool operator==(const ReadMostlySharedPtr<T, RefCount>& other) const {
    return get() == other.get();
  }

  ~ReadMostlyMainPtr() noexcept { reset(); }

  void reset() noexcept {
    if (impl_) {
      ptrRaw_ = nullptr;
      impl_->count_.useGlobal();
      impl_->weakCount_.useGlobal();
      impl_->decref();
      impl_ = nullptr;
    }
  }

  void reset(std::shared_ptr<T> ptr) {
    reset();
    if (ptr) {
      ptrRaw_ = ptr.get();
      impl_ = new detail::ReadMostlySharedPtrCore<RefCount>(std::move(ptr));
    }
  }

  T* get() const { return ptrRaw_; }

  std::shared_ptr<T> getStdShared() const {
    if (impl_) {
      return {impl_->getShared(), ptrRaw_};
    } else {
      return {};
    }
  }

  T& operator*() const { return *get(); }

  T* operator->() const { return get(); }

  ReadMostlySharedPtr<T, RefCount> getShared() const {
    return ReadMostlySharedPtr<T, RefCount>(*this);
  }

  explicit operator bool() const { return impl_ != nullptr; }

 private:
  template <typename U, typename RefCount2>
  friend class ReadMostlyWeakPtr;
  template <typename U, typename RefCount2>
  friend class ReadMostlySharedPtr;
  friend class ReadMostlyMainPtrDeleter<RefCount>;

  detail::ReadMostlySharedPtrCore<RefCount>* impl_{nullptr};
  T* ptrRaw_{nullptr};
};

template <typename T, typename RefCount = DefaultRefCount>
class ReadMostlyWeakPtr {
 public:
  ReadMostlyWeakPtr() {}

  ReadMostlyWeakPtr(const ReadMostlyWeakPtr& other) { *this = other; }

  ReadMostlyWeakPtr(ReadMostlyWeakPtr&& other) noexcept {
    *this = std::move(other);
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlyWeakPtr(const ReadMostlyWeakPtr<T2, RefCount>& other) {
    *this = other;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlyWeakPtr(ReadMostlyWeakPtr<T2, RefCount>&& other) noexcept {
    *this = std::move(other);
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  explicit ReadMostlyWeakPtr(const ReadMostlyMainPtr<T2, RefCount>& other) {
    *this = other;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  explicit ReadMostlyWeakPtr(const ReadMostlySharedPtr<T2, RefCount>& other) {
    *this = other;
  }

  ReadMostlyWeakPtr& operator=(const ReadMostlyWeakPtr& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  ReadMostlyWeakPtr& operator=(ReadMostlyWeakPtr&& other) noexcept {
    std::swap(impl_, other.impl_);
    std::swap(ptrRaw_, other.ptrRaw_);
    return *this;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlyWeakPtr& operator=(const ReadMostlyWeakPtr<T2, RefCount>& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlyWeakPtr& operator=(
      ReadMostlyWeakPtr<T2, RefCount>&& other) noexcept {
    reset();
    impl_ = std::exchange(other.impl_, nullptr);
    ptrRaw_ = std::exchange(other.ptrRaw_, nullptr);
    return *this;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlyWeakPtr& operator=(const ReadMostlyMainPtr<T2, RefCount>& mainPtr) {
    reset(mainPtr.impl_, mainPtr.ptrRaw_);
    return *this;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlyWeakPtr& operator=(
      const ReadMostlySharedPtr<T2, RefCount>& mainPtr) {
    reset(mainPtr.impl_, mainPtr.ptrRaw_);
    return *this;
  }

  ~ReadMostlyWeakPtr() noexcept { reset(nullptr, nullptr); }

  ReadMostlySharedPtr<T, RefCount> lock() {
    return ReadMostlySharedPtr<T, RefCount>(*this);
  }

 private:
  template <typename U, typename RefCount2>
  friend class ReadMostlyWeakPtr;
  template <typename U, typename RefCount2>
  friend class ReadMostlySharedPtr;

  void reset(detail::ReadMostlySharedPtrCore<RefCount>* impl, T* ptrRaw) {
    if (impl_ == impl) {
      return;
    }

    if (impl_) {
      impl_->decrefWeak();
    }
    impl_ = impl;
    ptrRaw_ = ptrRaw;
    if (impl_) {
      impl_->increfWeak();
    }
  }

  detail::ReadMostlySharedPtrCore<RefCount>* impl_{nullptr};
  T* ptrRaw_{nullptr};
};

template <typename T, typename RefCount = DefaultRefCount>
class ReadMostlySharedPtr {
 public:
  ReadMostlySharedPtr() {}

  ReadMostlySharedPtr(const ReadMostlySharedPtr& other) { *this = other; }

  ReadMostlySharedPtr(ReadMostlySharedPtr&& other) noexcept {
    *this = std::move(other);
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlySharedPtr(const ReadMostlySharedPtr<T2, RefCount>& other) {
    *this = other;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlySharedPtr(ReadMostlySharedPtr<T2, RefCount>&& other) noexcept {
    *this = std::move(other);
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  explicit ReadMostlySharedPtr(const ReadMostlyWeakPtr<T2, RefCount>& other) {
    *this = other;
  }

  // Generally, this shouldn't be used.
  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  explicit ReadMostlySharedPtr(const ReadMostlyMainPtr<T2, RefCount>& other) {
    *this = other;
  }

  ReadMostlySharedPtr& operator=(const ReadMostlySharedPtr& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  ReadMostlySharedPtr& operator=(ReadMostlySharedPtr&& other) noexcept {
    std::swap(impl_, other.impl_);
    std::swap(ptrRaw_, other.ptrRaw_);
    return *this;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlySharedPtr& operator=(
      const ReadMostlySharedPtr<T2, RefCount>& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlySharedPtr& operator=(
      ReadMostlySharedPtr<T2, RefCount>&& other) noexcept {
    reset();
    impl_ = std::exchange(other.impl_, nullptr);
    ptrRaw_ = std::exchange(other.ptrRaw_, nullptr);
    return *this;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlySharedPtr& operator=(const ReadMostlyWeakPtr<T2, RefCount>& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  template <
      typename T2,
      typename = std::enable_if_t<std::is_convertible<T2*, T*>::value>>
  ReadMostlySharedPtr& operator=(const ReadMostlyMainPtr<T2, RefCount>& other) {
    reset(other.impl_, other.ptrRaw_);
    return *this;
  }

  ~ReadMostlySharedPtr() noexcept { reset(nullptr, nullptr); }

  bool operator==(const ReadMostlyMainPtr<T, RefCount>& other) const {
    return get() == other.get();
  }

  bool operator==(T* other) const { return get() == other; }

  bool operator==(const ReadMostlySharedPtr<T, RefCount>& other) const {
    return get() == other.get();
  }

  void reset() { reset(nullptr, nullptr); }

  T* get() const { return ptrRaw_; }

  std::shared_ptr<T> getStdShared() const {
    if (impl_) {
      return {impl_->getShared(), ptrRaw_};
    } else {
      return {};
    }
  }

  T& operator*() const { return *get(); }

  T* operator->() const { return get(); }

  size_t use_count() const { return impl_->useCount(); }

  bool unique() const { return use_count() == 1; }

  explicit operator bool() const { return impl_ != nullptr; }

 private:
  template <typename U, typename RefCount2>
  friend class ReadMostlyWeakPtr;
  template <typename U, typename RefCount2>
  friend class ReadMostlySharedPtr;

  void reset(detail::ReadMostlySharedPtrCore<RefCount>* impl, T* ptrRaw) {
    if (impl_ == impl) {
      return;
    }

    if (impl_) {
      impl_->decref();
      impl_ = nullptr;
      ptrRaw_ = nullptr;
    }

    if (impl && impl->incref()) {
      impl_ = impl;
      ptrRaw_ = ptrRaw;
    }
  }

  T* ptrRaw_{nullptr};
  detail::ReadMostlySharedPtrCore<RefCount>* impl_{nullptr};
};

/**
 * This can be used to destroy multiple ReadMostlyMainPtrs at once.
 */
template <typename RefCount = DefaultRefCount>
class ReadMostlyMainPtrDeleter {
 public:
  ~ReadMostlyMainPtrDeleter() noexcept {
    RefCount::useGlobal(refCounts_);
    for (auto& decref : decrefs_) {
      decref();
    }
  }

  template <typename T>
  void add(ReadMostlyMainPtr<T, RefCount> ptr) noexcept {
    if (!ptr.impl_) {
      return;
    }

    refCounts_.push_back(&ptr.impl_->count_);
    refCounts_.push_back(&ptr.impl_->weakCount_);
    decrefs_.push_back([impl = ptr.impl_] { impl->decref(); });
    ptr.impl_ = nullptr;
    ptr.ptrRaw_ = nullptr;
  }

 private:
  std::vector<RefCount*> refCounts_;
  std::vector<folly::Function<void()>> decrefs_;
};

template <typename T, typename RefCount>
inline bool operator==(
    const ReadMostlyMainPtr<T, RefCount>& ptr, std::nullptr_t) {
  return ptr.get() == nullptr;
}

template <typename T, typename RefCount>
inline bool operator==(
    std::nullptr_t, const ReadMostlyMainPtr<T, RefCount>& ptr) {
  return ptr.get() == nullptr;
}

template <typename T, typename RefCount>
inline bool operator==(
    const ReadMostlySharedPtr<T, RefCount>& ptr, std::nullptr_t) {
  return ptr.get() == nullptr;
}

template <typename T, typename RefCount>
inline bool operator==(
    std::nullptr_t, const ReadMostlySharedPtr<T, RefCount>& ptr) {
  return ptr.get() == nullptr;
}

template <typename T, typename RefCount>
inline bool operator!=(
    const ReadMostlyMainPtr<T, RefCount>& ptr, std::nullptr_t) {
  return !(ptr == nullptr);
}

template <typename T, typename RefCount>
inline bool operator!=(
    std::nullptr_t, const ReadMostlyMainPtr<T, RefCount>& ptr) {
  return !(ptr == nullptr);
}

template <typename T, typename RefCount>
inline bool operator!=(
    const ReadMostlySharedPtr<T, RefCount>& ptr, std::nullptr_t) {
  return !(ptr == nullptr);
}

template <typename T, typename RefCount>
inline bool operator!=(
    std::nullptr_t, const ReadMostlySharedPtr<T, RefCount>& ptr) {
  return !(ptr == nullptr);
}
} // namespace folly
