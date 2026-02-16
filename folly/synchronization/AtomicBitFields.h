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

#include <folly/lang/BitFields.h>
#include <folly/synchronization/AtomicStruct.h>

namespace folly {

// A handy wrapper for an atomic on some bit_fields type.
//
// For encapsulation, usual arithmetic atomic operations are only available by
// calling applyRelaxed() on Transforms returned from field classes. Example:
//
// auto transform = Field2::clearTransform() + Field4::clearTransform();
// MyState old_state;
// my_atomic.applyRelaxed(transform, &old_state);
// auto field2_before_clearing = old_state.get<Field2>();
//
template <typename BitFieldsT, template <typename> class Atom = std::atomic>
class atomic_bit_fields : AtomicStruct<BitFieldsT, Atom> {
 private:
  using underlying_type = typename BitFieldsT::underlying_type;
  using base = AtomicStruct<BitFieldsT, Atom>;

 public:
  using value_type = BitFieldsT;

  atomic_bit_fields() = default;
  ~atomic_bit_fields() = default;
  constexpr /* implicit */ atomic_bit_fields(BitFieldsT v) noexcept : base(v) {}

  atomic_bit_fields(const atomic_bit_fields& other) noexcept
      : base(other.load()) {}
  atomic_bit_fields& operator=(const atomic_bit_fields& other) noexcept {
    this->store(other.load());
    return *this;
  }

  atomic_bit_fields(atomic_bit_fields&&) = delete;
  atomic_bit_fields& operator=(atomic_bit_fields&&) = delete;

  using base::compare_exchange_strong;
  using base::compare_exchange_weak;
  using base::exchange;
  using base::is_lock_free;
  using base::load;
  using base::operator BitFieldsT;
  using base::store;

  BitFieldsT operator=(BitFieldsT v) noexcept {
    store(v);
    return v;
  }

  void apply(
      const or_transformer<BitFieldsT>& transform,
      std::memory_order mo = std::memory_order_seq_cst,
      BitFieldsT* before = nullptr,
      BitFieldsT* after = nullptr) {
    underlying_type before_val = base::data.fetch_or(transform.to_or, mo);
    if (before) {
      before->underlying = before_val;
    }
    if (after) {
      after->underlying = before_val | transform.to_or;
    }
  }

  void apply(
      const and_transformer<BitFieldsT>& transform,
      std::memory_order mo = std::memory_order_seq_cst,
      BitFieldsT* before = nullptr,
      BitFieldsT* after = nullptr) {
    underlying_type before_val = base::data.fetch_and(transform.to_and, mo);
    if (before) {
      before->underlying = before_val;
    }
    if (after) {
      after->underlying = before_val & transform.to_and;
    }
  }

  void apply(
      const add_transformer<BitFieldsT>& transform,
      std::memory_order mo = std::memory_order_seq_cst,
      BitFieldsT* before = nullptr,
      BitFieldsT* after = nullptr) {
    underlying_type before_val = base::data.fetch_add(transform.to_add, mo);
    transform.assertPreconditions(before_val);
    if (before) {
      before->underlying = before_val;
    }
    if (after) {
      after->underlying = before_val + transform.to_add;
    }
  }
};

} // namespace folly
