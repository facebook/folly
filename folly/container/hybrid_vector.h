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

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <folly/Preprocessor.h>
#include <folly/lang/CheckedMath.h>
#include <folly/lang/Exception.h>
#include <folly/lang/SafeAssert.h>
#include <folly/memory/HybridAlloc.h>

namespace folly {

template <class T>
class hybrid_vector;

namespace detail::hybrid_vector_detail {

template <class T>
hybrid_vector<T> adopt_storage(void* storage, size_t capacity);

template <class R>
concept sized_or_forward_range = std::ranges::input_range<R> &&
    (std::ranges::sized_range<R> || std::ranges::forward_range<R>);

template <class T>
size_t storage_size(size_t capacity) {
  size_t bytes;
  if (!folly::checked_mul(&bytes, capacity, sizeof(T))) {
    folly::throw_exception<std::bad_alloc>();
  }
  return bytes;
}

template <class T>
constexpr size_t effective_alignment(size_t alignment) noexcept {
  return alignof(T) > alignment ? alignof(T) : alignment;
}

template <class R>
size_t range_size(R&& range) {
  if constexpr (std::ranges::sized_range<R>) {
    return static_cast<size_t>(std::ranges::size(range));
  } else {
    return static_cast<size_t>(std::ranges::distance(range));
  }
}

} // namespace detail::hybrid_vector_detail

/// Fixed-capacity vector backed by HybridAlloc.
///
/// Capacity is chosen at construction time and never grows. Storage is usually
/// stack-backed through the declaration macros below, with heap fallback when
/// the requested capacity exceeds the threshold.
///
/// Move is only valid for heap-backed or empty vectors. Use to_persistent()
/// before moving a stack-backed instance.
template <class T>
class hybrid_vector {
  static_assert(std::is_object_v<T>, "hybrid_vector requires an object type");
  static_assert(
      !std::is_const_v<T> && !std::is_volatile_v<T>,
      "hybrid_vector requires a non-cv-qualified element type");

 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using iterator = value_type*;
  using const_iterator = const value_type*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  hybrid_vector() noexcept = default;

  /// Creates a heap-only vector with fixed capacity.
  static hybrid_vector persistent(size_type capacity) {
    if (capacity == 0) {
      return hybrid_vector{};
    }

    const auto bytes = detail::hybrid_vector_detail::storage_size<T>(capacity);
    void* storage = detail::hybridHeapAlloc(
        bytes, alignof(T), kHybridAlignedAllocHeapMarker);
    return hybrid_vector{storage, capacity};
  }

  hybrid_vector(const hybrid_vector&) = delete;
  hybrid_vector& operator=(const hybrid_vector&) = delete;

  /// Moves heap-backed storage; stack-backed vectors must be made persistent.
  hybrid_vector(hybrid_vector&& other) noexcept { move_from(std::move(other)); }

  hybrid_vector& operator=(hybrid_vector&& other) noexcept {
    if (this != &other) {
      reset();
      move_from(std::move(other));
    }
    return *this;
  }

  ~hybrid_vector() noexcept { reset(); }

  iterator begin() noexcept { return data_; }
  const_iterator begin() const noexcept { return data_; }
  const_iterator cbegin() const noexcept { return begin(); }

  iterator end() noexcept { return end_ptr(); }
  const_iterator end() const noexcept { return end_ptr(); }
  const_iterator cend() const noexcept { return end(); }

  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const noexcept {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const noexcept {
    return const_reverse_iterator(begin());
  }
  const_reverse_iterator crend() const noexcept { return rend(); }

  bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  size_type capacity() const noexcept { return capacity_; }
  size_type max_size() const noexcept { return capacity_; }

  /// Checks that `n` fits the fixed capacity.
  void reserve(size_type n) { check_reserve_capacity(n); }

  /// No-op: capacity is fixed.
  void shrink_to_fit() noexcept {}

  /// Pointer to the contiguous element storage.
  pointer data() noexcept { return data_; }
  /// Pointer to the contiguous element storage.
  const_pointer data() const noexcept { return data_; }

  reference operator[](size_type pos) noexcept {
    FOLLY_SAFE_DCHECK(pos < size_, "hybrid_vector index out of range");
    return data_[pos];
  }

  const_reference operator[](size_type pos) const noexcept {
    FOLLY_SAFE_DCHECK(pos < size_, "hybrid_vector index out of range");
    return data_[pos];
  }

  reference at(size_type pos) {
    if (pos >= size_) {
      folly::throw_exception<std::out_of_range>(
          "hybrid_vector index out of range");
    }
    return data_[pos];
  }

  const_reference at(size_type pos) const {
    if (pos >= size_) {
      folly::throw_exception<std::out_of_range>(
          "hybrid_vector index out of range");
    }
    return data_[pos];
  }

  reference front() noexcept {
    FOLLY_SAFE_DCHECK(!empty(), "hybrid_vector::front on empty vector");
    return data_[0];
  }

  const_reference front() const noexcept {
    FOLLY_SAFE_DCHECK(!empty(), "hybrid_vector::front on empty vector");
    return data_[0];
  }

  reference back() noexcept {
    FOLLY_SAFE_DCHECK(!empty(), "hybrid_vector::back on empty vector");
    return data_[size_ - 1];
  }

  const_reference back() const noexcept {
    FOLLY_SAFE_DCHECK(!empty(), "hybrid_vector::back on empty vector");
    return data_[size_ - 1];
  }

  template <class... Args>
  reference emplace_back(Args&&... args) {
    check_capacity(checked_size_plus(1));
    return unchecked_emplace_back(std::forward<Args>(args)...);
  }

  /// Appends if capacity remains, otherwise returns nullptr.
  template <class... Args>
  [[nodiscard]] pointer try_emplace_back(Args&&... args) {
    if (size_ == capacity_) {
      return nullptr;
    }
    return std::addressof(unchecked_emplace_back(std::forward<Args>(args)...));
  }

  /// Appends without a runtime capacity check.
  template <class... Args>
  reference unchecked_emplace_back(Args&&... args) {
    FOLLY_SAFE_DCHECK(
        size_ < capacity_, "hybrid_vector unchecked append exceeds capacity");
    pointer slot = data_ + size_;
    std::construct_at(slot, std::forward<Args>(args)...);
    ++size_;
    return *slot;
  }

  reference push_back(const value_type& value) { return emplace_back(value); }

  reference push_back(value_type&& value) {
    return emplace_back(std::move(value));
  }

  [[nodiscard]] pointer try_push_back(const value_type& value) {
    return try_emplace_back(value);
  }

  [[nodiscard]] pointer try_push_back(value_type&& value) {
    return try_emplace_back(std::move(value));
  }

  reference unchecked_push_back(const value_type& value) {
    return unchecked_emplace_back(value);
  }

  reference unchecked_push_back(value_type&& value) {
    return unchecked_emplace_back(std::move(value));
  }

  void pop_back() noexcept(std::is_nothrow_destructible_v<T>) {
    FOLLY_SAFE_DCHECK(!empty(), "hybrid_vector::pop_back on empty vector");
    --size_;
    destroy_at(data_ + size_);
  }

  void resize(size_type count)
    requires std::default_initializable<T>
  {
    if (count < size_) {
      destroy_suffix(count);
      return;
    }

    check_capacity(count);
    const auto oldSize = size_;
    try {
      while (size_ < count) {
        std::construct_at(data_ + size_);
        ++size_;
      }
    } catch (...) {
      destroy_suffix(oldSize);
      throw;
    }
  }

  void resize(size_type count, const value_type& value)
    requires std::copy_constructible<T>
  {
    if (count < size_) {
      destroy_suffix(count);
      return;
    }

    check_capacity(count);
    const auto oldSize = size_;
    try {
      while (size_ < count) {
        std::construct_at(data_ + size_, value);
        ++size_;
      }
    } catch (...) {
      destroy_suffix(oldSize);
      throw;
    }
  }

  /// Destroys all elements. Does not release the backing allocation.
  void clear() noexcept(std::is_nothrow_destructible_v<T>) {
    destroy_suffix(0);
  }

  /// Destroys all elements and releases the backing allocation.
  void reset() noexcept(std::is_nothrow_destructible_v<T>) {
    clear();
    hybridFree(data_);
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
  }

  void assign(size_type count, const value_type& value)
    requires std::copy_constructible<T>
  {
    check_capacity(count);
    clear();
    resize(count, value);
  }

  template <std::input_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  void assign(InputIt first, Sentinel last)
    requires std::forward_iterator<InputIt>
  {
    const auto count =
        static_cast<size_type>(std::ranges::distance(first, last));
    check_capacity(count);
    clear();
    for (; first != last; ++first) {
      unchecked_emplace_back(*first);
    }
  }

  void assign(std::initializer_list<value_type> init)
    requires std::copy_constructible<T>
  {
    assign(init.begin(), init.end());
  }

  template <detail::hybrid_vector_detail::sized_or_forward_range R>
  void assign_range(R&& range) {
    const auto count = detail::hybrid_vector_detail::range_size(range);
    check_capacity(count);
    clear();
    append_range(std::forward<R>(range));
  }

  template <detail::hybrid_vector_detail::sized_or_forward_range R>
  void append_range(R&& range) {
    const auto count = detail::hybrid_vector_detail::range_size(range);
    check_capacity(checked_size_plus(count));
    unchecked_append_range(std::forward<R>(range));
  }

  /// Appends as many values as will fit and returns the count appended.
  template <std::ranges::input_range R>
  [[nodiscard]] size_type try_append_range(R&& range) {
    size_type appended = 0;
    auto it = std::ranges::begin(range);
    auto last = std::ranges::end(range);
    for (; it != last && size_ < capacity_; ++it) {
      unchecked_emplace_back(*it);
      ++appended;
    }
    return appended;
  }

  template <std::ranges::input_range R>
  void unchecked_append_range(R&& range) {
    for (auto&& item : range) {
      unchecked_emplace_back(std::forward<decltype(item)>(item));
    }
  }

  iterator insert(const_iterator pos, const value_type& value)
    requires std::copy_constructible<T> && std::assignable_from<T&, T>
  {
    value_type copy(value);
    return insert_value(pos, std::move(copy));
  }

  iterator insert(const_iterator pos, value_type&& value)
    requires std::move_constructible<T> && std::assignable_from<T&, T>
  {
    return insert_value(pos, std::move(value));
  }

  iterator insert(const_iterator pos, size_type count, const value_type& value)
    requires std::copy_constructible<T> && std::assignable_from<T&, T>
  {
    const auto offset = offset_of(pos);
    check_capacity(checked_size_plus(count));
    value_type copy(value);
    for (size_type i = 0; i < count; ++i) {
      value_type item(copy);
      insert_value(
          cbegin() + static_cast<difference_type>(offset + i), std::move(item));
    }
    return begin() + static_cast<difference_type>(offset);
  }

  template <std::forward_iterator InputIt, std::sentinel_for<InputIt> Sentinel>
  iterator insert(const_iterator pos, InputIt first, Sentinel last)
    requires std::copy_constructible<T> && std::assignable_from<T&, T>
  {
    const auto offset = offset_of(pos);
    const auto count =
        static_cast<size_type>(std::ranges::distance(first, last));
    check_capacity(checked_size_plus(count));
    size_type inserted = 0;
    for (; first != last; ++first, ++inserted) {
      value_type item(*first);
      insert_value(
          cbegin() + static_cast<difference_type>(offset + inserted),
          std::move(item));
    }
    return begin() + static_cast<difference_type>(offset);
  }

  iterator insert(const_iterator pos, std::initializer_list<value_type> init)
    requires std::copy_constructible<T> && std::assignable_from<T&, T>
  {
    return insert(pos, init.begin(), init.end());
  }

  template <detail::hybrid_vector_detail::sized_or_forward_range R>
  iterator insert_range(const_iterator pos, R&& range)
    requires std::copy_constructible<T> && std::assignable_from<T&, T>
  {
    const auto offset = offset_of(pos);
    const auto count = detail::hybrid_vector_detail::range_size(range);
    check_capacity(checked_size_plus(count));
    size_type inserted = 0;
    for (auto&& item : range) {
      value_type value(item);
      insert_value(
          cbegin() + static_cast<difference_type>(offset + inserted),
          std::move(value));
      ++inserted;
    }
    return begin() + static_cast<difference_type>(offset);
  }

  template <class... Args>
  iterator emplace(const_iterator pos, Args&&... args)
    requires std::move_constructible<T> && std::assignable_from<T&, T>
  {
    const auto offset = offset_of(pos);
    if (offset == size_) {
      emplace_back(std::forward<Args>(args)...);
      return end() - 1;
    }
    value_type value(std::forward<Args>(args)...);
    return insert_value(pos, std::move(value));
  }

  iterator erase(const_iterator pos)
    requires std::assignable_from<T&, T>
  {
    return erase(pos, pos + 1);
  }

  iterator erase(const_iterator first, const_iterator last)
    requires std::assignable_from<T&, T>
  {
    const auto beginOffset = offset_of(first);
    const auto endOffset = offset_of(last);
    FOLLY_SAFE_DCHECK(
        beginOffset <= endOffset, "invalid hybrid_vector erase range");
    std::move(
        begin() + static_cast<difference_type>(endOffset),
        end(),
        begin() + static_cast<difference_type>(beginOffset));
    destroy_suffix(size_ - (endOffset - beginOffset));
    return begin() + static_cast<difference_type>(beginOffset);
  }

  /// Swaps elements via pointer swap when both sides are heap-backed or empty;
  /// otherwise falls back to element-wise swap (which requires capacity for the
  /// other side's elements on each side).
  void swap(hybrid_vector& other) {
    if (this == &other) {
      return;
    }
    if (data_ == nullptr || other.data_ == nullptr ||
        is_on_heap() || other.is_on_heap()) {
      std::swap(data_, other.data_);
      std::swap(size_, other.size_);
      std::swap(capacity_, other.capacity_);
      return;
    }

    check_capacity(other.size_);
    other.check_capacity(size_);
    swap_elements(other);
  }

  /// Whether the backing storage came from the heap.
  [[nodiscard]] bool is_on_heap() const noexcept {
    return data_ != nullptr &&
        detail::isHybridAllocHeapMarker(
               detail::hybridAllocHeader(data_)->marker);
  }

  /// Whether the backing storage lives on the stack.
  [[nodiscard]] bool is_on_stack() const noexcept {
    return data_ != nullptr &&
        detail::isHybridAllocStackMarker(
               detail::hybridAllocHeader(data_)->marker);
  }

  /// Returns a heap-only copy, suitable for move or long-lived storage.
  [[nodiscard]] hybrid_vector to_persistent() const&
    requires std::copy_constructible<T>
  {
    auto out = hybrid_vector::persistent(capacity_);
    out.append_range(*this);
    return out;
  }

  /// Converts stack-backed storage to heap and leaves this vector empty.
  [[nodiscard]] hybrid_vector to_persistent() &&
    requires std::move_constructible<T>
  {
    if (is_on_heap() || data_ == nullptr) {
      return std::move(*this);
    }

    auto out = hybrid_vector::persistent(capacity_);
    for (auto& item : *this) {
      out.unchecked_emplace_back(std::move(item));
    }
    clear();
    return out;
  }

  /// Copies or moves elements into an array, value-initializing the tail.
  template <size_t N>
  [[nodiscard]] std::array<T, N> to_array() const&
    requires std::copy_constructible<T> && std::default_initializable<T>
  {
    check_array_capacity<N>();
    return to_array_default_impl<N>(std::make_index_sequence<N>{});
  }

  template <size_t N>
  [[nodiscard]] std::array<T, N> to_array() &&
    requires std::move_constructible<T> && std::default_initializable<T>
  {
    check_array_capacity<N>();
    return std::move(*this).template to_array_default_impl<N>(
        std::make_index_sequence<N>{});
  }

  /// Copies or moves elements into an array, filling the tail with `fill`.
  template <size_t N>
  [[nodiscard]] std::array<T, N> to_array(const T& fill) const&
    requires std::copy_constructible<T>
  {
    check_array_capacity<N>();
    return to_array_fill_impl<N>(fill, std::make_index_sequence<N>{});
  }

  template <size_t N>
  [[nodiscard]] std::array<T, N> to_array(const T& fill) &&
    requires std::move_constructible<T> && std::copy_constructible<T>
  {
    check_array_capacity<N>();
    return std::move(*this).template to_array_fill_impl<N>(
        fill, std::make_index_sequence<N>{});
  }

  /// Converts to an array only when the sizes match exactly.
  template <size_t N>
  [[nodiscard]] std::array<T, N> to_array_exact() const&
    requires std::copy_constructible<T>
  {
    check_array_exact_size<N>();
    return to_array_exact_impl<N>(std::make_index_sequence<N>{});
  }

  template <size_t N>
  [[nodiscard]] std::array<T, N> to_array_exact() &&
    requires std::move_constructible<T>
  {
    check_array_exact_size<N>();
    return std::move(*this).template to_array_exact_impl<N>(
        std::make_index_sequence<N>{});
  }

  friend bool operator==(const hybrid_vector& lhs, const hybrid_vector& rhs) {
    return lhs.size() == rhs.size() &&
        std::equal(lhs.begin(), lhs.end(), rhs.begin());
  }

  template <typename U = value_type>
  friend auto operator<=>(const hybrid_vector& lhs, const hybrid_vector& rhs)
      -> decltype(std::declval<const U&>() <=> std::declval<const U&>()) {
    return std::lexicographical_compare_three_way(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
  }

 private:
  template <class U>
  friend hybrid_vector<U> detail::hybrid_vector_detail::adopt_storage(
      void*, size_t);

  pointer data_{nullptr};
  size_type size_{0};
  size_type capacity_{0};

  hybrid_vector(void* storage, size_type cap)
      : data_(static_cast<pointer>(storage)), capacity_(cap) {
    if (capacity_ != 0 && data_ == nullptr) {
      folly::throw_exception<std::bad_alloc>();
    }
  }

  static void throw_capacity_exceeded() {
    folly::throw_exception<std::bad_alloc>();
  }

  static void throw_length_error_capacity_exceeded() {
    folly::throw_exception<std::length_error>(
        "hybrid_vector capacity exceeded");
  }

  void check_capacity(size_type requested) const {
    if (requested > capacity_) {
      throw_capacity_exceeded();
    }
  }

  void check_reserve_capacity(size_type requested) const {
    if (requested > capacity_) {
      throw_length_error_capacity_exceeded();
    }
  }

  size_type checked_size_plus(size_type extra) const {
    size_type requested;
    if (!folly::checked_add(&requested, size_, extra)) {
      throw_capacity_exceeded();
    }
    return requested;
  }

  pointer end_ptr() noexcept {
    return data_ == nullptr ? nullptr : data_ + size_;
  }

  const_pointer end_ptr() const noexcept {
    return data_ == nullptr ? nullptr : data_ + size_;
  }

  size_type offset_of(const_iterator pos) const {
    FOLLY_SAFE_DCHECK(
        pos >= cbegin() && pos <= cend(),
        "hybrid_vector iterator out of range");
    if (data_ == nullptr) {
      return 0;
    }
    return static_cast<size_type>(pos - cbegin());
  }

  void move_from(hybrid_vector&& other) noexcept {
    if (other.data_ == nullptr) {
      return;
    }
    FOLLY_SAFE_CHECK(
        other.is_on_heap(),
        "cannot move a stack-backed hybrid_vector; call to_persistent() first");
    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
  }

  static void destroy_at(pointer ptr) noexcept(
      std::is_nothrow_destructible_v<T>) {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      std::destroy_at(ptr);
    }
  }

  void destroy_suffix(size_type newSize) noexcept(
      std::is_nothrow_destructible_v<T>) {
    FOLLY_SAFE_DCHECK(newSize <= size_, "invalid hybrid_vector resize");
    if constexpr (!std::is_trivially_destructible_v<T>) {
      std::destroy(data_ + newSize, data_ + size_);
    }
    size_ = newSize;
  }

  iterator insert_value(const_iterator pos, value_type&& value)
    requires std::move_constructible<T> && std::assignable_from<T&, T>
  {
    const auto offset = offset_of(pos);
    check_capacity(checked_size_plus(1));
    if (offset == size_) {
      unchecked_emplace_back(std::move(value));
      return end() - 1;
    }

    std::construct_at(data_ + size_, std::move(data_[size_ - 1]));
    ++size_;
    for (size_type i = size_ - 2; i > offset; --i) {
      data_[i] = std::move(data_[i - 1]);
    }
    data_[offset] = std::move(value);
    return begin() + static_cast<difference_type>(offset);
  }

  void swap_elements(hybrid_vector& other) {
    using std::swap;
    const auto common = size_ < other.size_ ? size_ : other.size_;
    for (size_type i = 0; i < common; ++i) {
      swap(data_[i], other.data_[i]);
    }

    if (size_ < other.size_) {
      const auto oldSize = size_;
      for (size_type i = oldSize; i < other.size_; ++i) {
        unchecked_emplace_back(std::move(other.data_[i]));
      }
      other.destroy_suffix(common);
      return;
    }

    const auto oldOtherSize = other.size_;
    for (size_type i = oldOtherSize; i < size_; ++i) {
      other.unchecked_emplace_back(std::move(data_[i]));
    }
    destroy_suffix(common);
  }

  template <size_t N>
  void check_array_capacity() const {
    if (size_ > N) {
      folly::throw_exception<std::length_error>(
          "hybrid_vector has too many elements for std::array");
    }
  }

  template <size_t N>
  void check_array_exact_size() const {
    if (size_ != N) {
      folly::throw_exception<std::length_error>(
          "hybrid_vector size does not match std::array size");
    }
  }

  template <size_t N, size_t... I>
  std::array<T, N> to_array_default_impl(std::index_sequence<I...>) const& {
    return {{(I < size_ ? data_[I] : T{})...}};
  }

  template <size_t N, size_t... I>
  std::array<T, N> to_array_default_impl(std::index_sequence<I...>) && {
    return {{(I < size_ ? std::move(data_[I]) : T{})...}};
  }

  template <size_t N, size_t... I>
  std::array<T, N> to_array_fill_impl(
      const T& fill, std::index_sequence<I...>) const& {
    return {{(I < size_ ? data_[I] : fill)...}};
  }

  template <size_t N, size_t... I>
  std::array<T, N> to_array_fill_impl(
      const T& fill, std::index_sequence<I...>) && {
    return {{(I < size_ ? std::move(data_[I]) : fill)...}};
  }

  template <size_t N, size_t... I>
  std::array<T, N> to_array_exact_impl(std::index_sequence<I...>) const& {
    return {{data_[I]...}};
  }

  template <size_t N, size_t... I>
  std::array<T, N> to_array_exact_impl(std::index_sequence<I...>) && {
    return {{std::move(data_[I])...}};
  }
};

namespace detail::hybrid_vector_detail {

template <class T>
hybrid_vector<T> adopt_storage(void* storage, size_t capacity) {
  return hybrid_vector<T>{storage, capacity};
}

} // namespace detail::hybrid_vector_detail

template <class T>
void swap(hybrid_vector<T>& lhs, hybrid_vector<T>& rhs) {
  lhs.swap(rhs);
}

template <class T, class U>
typename hybrid_vector<T>::size_type erase(
    hybrid_vector<T>& c, const U& value) {
  const auto oldSize = c.size();
  c.erase(std::remove(c.begin(), c.end(), value), c.end());
  return oldSize - c.size();
}

template <class T, class Pred>
typename hybrid_vector<T>::size_type erase_if(hybrid_vector<T>& c, Pred pred) {
  const auto oldSize = c.size();
  c.erase(std::remove_if(c.begin(), c.end(), pred), c.end());
  return oldSize - c.size();
}

} // namespace folly

namespace std {

template <class T>
struct hash<folly::hybrid_vector<T>> {
  size_t operator()(const folly::hybrid_vector<T>& value) const {
    size_t seed = 0;
    std::hash<T> hasher;
    for (const auto& item : value) {
      seed ^= hasher(item) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};

} // namespace std

/// Implementation detail for declaration macro local names.
///
/// The public macros are statement-like on purpose. Storage may come from
/// alloca, so keeping the allocation in the caller's frame avoids returning a
/// vector that points at a dead stack frame.
#define FOLLY_HYBRID_VECTOR_DETAIL_CAPACITY(name) \
  FB_CONCATENATE(folly_hybrid_vector_capacity_, name)
#define FOLLY_HYBRID_VECTOR_DETAIL_ALIGNMENT(name) \
  FB_CONCATENATE(folly_hybrid_vector_alignment_, name)
#define FOLLY_HYBRID_VECTOR_DETAIL_THRESHOLD(name) \
  FB_CONCATENATE(folly_hybrid_vector_threshold_, name)
#define FOLLY_HYBRID_VECTOR_DETAIL_BYTES(name) \
  FB_CONCATENATE(folly_hybrid_vector_bytes_, name)

/// Declares a fixed-capacity hybrid_vector with explicit alignment and
/// threshold.
///
/// The storage is stack-backed when it fits the threshold and heap-backed
/// otherwise. Must be used as a declaration.
///
///   FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(int, vec, 128, 64, 1024);
///   vec.push_back(42);
#define FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(                           \
    T, name, capacity, alignment, threshold)                             \
  const ::folly::hybrid_vector<T>::size_type                             \
      FOLLY_HYBRID_VECTOR_DETAIL_CAPACITY(name) = (capacity);            \
  const size_t FOLLY_HYBRID_VECTOR_DETAIL_ALIGNMENT(name) =              \
      ::folly::detail::hybrid_vector_detail::effective_alignment<T>(     \
          alignment);                                                    \
  const size_t FOLLY_HYBRID_VECTOR_DETAIL_THRESHOLD(name) = (threshold); \
  const size_t FOLLY_HYBRID_VECTOR_DETAIL_BYTES(name) =                  \
      ::folly::detail::hybrid_vector_detail::storage_size<T>(            \
          FOLLY_HYBRID_VECTOR_DETAIL_CAPACITY(name));                    \
  auto name = ::folly::detail::hybrid_vector_detail::adopt_storage<T>(   \
      FOLLY_HYBRID_VECTOR_DETAIL_CAPACITY(name) == 0                     \
          ? nullptr                                                      \
          : FOLLY_HYBRID_ALIGNED_ALLOC_THRESHOLD(                        \
                FOLLY_HYBRID_VECTOR_DETAIL_BYTES(name),                  \
                FOLLY_HYBRID_VECTOR_DETAIL_ALIGNMENT(name),              \
                FOLLY_HYBRID_VECTOR_DETAIL_THRESHOLD(name)),             \
      FOLLY_HYBRID_VECTOR_DETAIL_CAPACITY(name))

/// Like FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD, with the default threshold.
#define FOLLY_HYBRID_VECTOR_ALIGNED(T, name, capacity, alignment) \
  FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(                          \
      T, name, capacity, alignment, FOLLY_HYBRID_ALLOC_DEFAULT_THRESHOLD)

/// Like FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD, with natural alignment for T.
#define FOLLY_HYBRID_VECTOR_THRESHOLD(T, name, capacity, threshold) \
  FOLLY_HYBRID_VECTOR_ALIGNED_THRESHOLD(                            \
      T, name, capacity, alignof(T), threshold)

/// Default convenience macro: stack-first vector with natural alignment.
#define FOLLY_HYBRID_VECTOR(T, name, capacity) \
  FOLLY_HYBRID_VECTOR_THRESHOLD(               \
      T, name, capacity, FOLLY_HYBRID_ALLOC_DEFAULT_THRESHOLD)
