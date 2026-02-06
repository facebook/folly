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

#include <cassert>
#include <type_traits>
#include <vector>

#include <folly/lang/Bits.h>

namespace folly {

// Forward declarations for transformers
template <typename BitFieldsT>
struct or_transformer;
template <typename BitFieldsT>
struct and_transformer;
template <typename BitFieldsT>
struct add_transformer;

// Declares a wrapper type around UnderlyingT that allows it to be divided up
// into and accessed as bit fields. This is mostly intended to aid in packing
// fields into atomic variables to reduce the need for locking in concurrent
// code and/or to simplify reasoning on and accommodation of different
// interesting, bug-prone interleavings. Convenient atomic wrappers
// (relaxed_bit_fields_atomic, acq_rel_bit_fields_atomic) are provided in
// folly/synchronization/AtomicBitFields.h to aid usage with atomics,
// especially for CAS updates, but it is even possible to combine operations on
// multiple bit fields into a single non-CAS atomic operation using Transforms.
//
// Unlike C/C++ bit fields, this implementation guarantees tight bit packing
// so that all available lock-free atomic bits can be utilized.
//
// The specific bit fields are declared outside the declaration using
// bool_bit_field and unsigned_bit_field below. Example usage:
//
// struct MyState : public bit_fields<uint32_t, MyState> {
//   // Extra helper declarations and/or field type declarations
// };
//
// // Starts with a 16-bit field returned as uint16_t
// using Field1 = unsigned_bit_field<MyState, 16, no_prev_bit_field>;
// using Field2 = bool_bit_field<MyState, Field1>;
// using Field3 = bool_bit_field<MyState, Field2>;
// using Field4 = unsigned_bit_field<MyState, 5, Field3>;  // 5 bits in uint8_t
//
// // MyState{} is zero-initialized
// auto state = MyState{}.with<Field1>(42U).with<Field2>(true);
// state.set<Field4>(3U);
// state.ref<Field1>() += state.get<Field4>();
//
// Note that there's nothing preventing you from declaring overlapping fields
// in the same 'MyState' family. This could be useful for variant types where
// an earlier field determines which layout later fields are using. For
// example, an alternate field after Field2:
//
// using Field3a = unsigned_bit_field<State, 6, Field2>;  // 6 bits in uint8_t
//
template <typename UnderlyingT, typename DerivedT>
struct bit_fields {
  using underlying_type = UnderlyingT;
  underlying_type underlying = 0;
  static constexpr int kBitCount = sizeof(underlying_type) * 8;

  using derived_type = DerivedT;

  // Modify a given field in place
  template <typename BitFieldT>
  void set(typename BitFieldT::value_type value) {
    static_assert(
        std::is_same_v<typename BitFieldT::parent_type, derived_type>);
    derived_type& derived = static_cast<derived_type&>(*this);
    BitFieldT::setIn(derived, value);
  }

  // Return a copy with the given field modified
  template <typename BitFieldT>
  constexpr derived_type with(typename BitFieldT::value_type value) const {
    static_assert(
        std::is_same_v<typename BitFieldT::parent_type, derived_type>);
    derived_type rv = static_cast<const derived_type&>(*this);
    BitFieldT::setIn(rv, value);
    return rv;
  }

  // Get the value of a field
  template <typename BitFieldT>
  constexpr typename BitFieldT::value_type get() const {
    static_assert(
        std::is_same_v<typename BitFieldT::parent_type, derived_type>);
    return BitFieldT::getFrom(static_cast<const derived_type&>(*this));
  }

  // Reference and ref() are not intended to behave as full references but to
  // provide a convenient way to do operations like +=, |=, etc. get and set
  // are preferred for simple operations.
  template <typename BitFieldT>
  struct reference {
    explicit reference(bit_fields& bf) : bf_(bf) {}
    reference(const reference&) = default;
    ~reference() = default;
    reference& operator=(const reference&) = default;
    reference(reference&&) noexcept = default;
    reference& operator=(reference&&) noexcept = default;

    void operator=(typename BitFieldT::value_type value) {
      bf_.set<BitFieldT>(value);
    }
    void operator+=(typename BitFieldT::value_type value) {
      bf_.set<BitFieldT>(bf_.get<BitFieldT>() + value);
    }
    void operator-=(typename BitFieldT::value_type value) {
      bf_.set<BitFieldT>(bf_.get<BitFieldT>() - value);
    }
    void operator|=(typename BitFieldT::value_type value) {
      bf_.set<BitFieldT>(bf_.get<BitFieldT>() | value);
    }
    void operator&=(typename BitFieldT::value_type value) {
      bf_.set<BitFieldT>(bf_.get<BitFieldT>() & value);
    }

   private:
    bit_fields& bf_;
  };

  template <typename BitFieldT>
  reference<BitFieldT> ref() {
    return reference<BitFieldT>(*this);
  }

  // Apply an or_transformer to update the underlying value
  void apply(const or_transformer<derived_type>& t) { underlying |= t.to_or; }

  // Apply an and_transformer to update the underlying value
  void apply(const and_transformer<derived_type>& t) { underlying &= t.to_and; }

  // Apply an add_transformer to update the underlying value
  void apply(const add_transformer<derived_type>& t) {
    t.assertPreconditions(underlying);
    underlying += t.to_add;
  }

  // Return a copy with an or_transformer applied
  constexpr derived_type transformed(
      const or_transformer<derived_type>& t) const {
    derived_type rv = static_cast<const derived_type&>(*this);
    rv.underlying |= t.to_or;
    return rv;
  }

  // Return a copy with an and_transformer applied
  constexpr derived_type transformed(
      const and_transformer<derived_type>& t) const {
    derived_type rv = static_cast<const derived_type&>(*this);
    rv.underlying &= t.to_and;
    return rv;
  }

  // Return a copy with an add_transformer applied
  derived_type transformed(const add_transformer<derived_type>& t) const {
    t.assertPreconditions(underlying);
    derived_type rv = static_cast<const derived_type&>(*this);
    rv.underlying += t.to_add;
    return rv;
  }

  // NOTE: use = default with C++20
  constexpr bool operator==(const bit_fields& other) const {
    return underlying == other.underlying;
  }
  // NOTE: use = default with C++20
  constexpr bool operator!=(const bit_fields& other) const {
    return underlying != other.underlying;
  }
};

// For building atomic updates affecting one or more fields, assuming all the
// updates are bitwise-or.
template <typename BitFieldsT>
struct or_transformer {
  using underlying_type = typename BitFieldsT::underlying_type;
  underlying_type to_or = 0;
  // + for general combine
  or_transformer<BitFieldsT> operator+(
      const or_transformer<BitFieldsT>& other) const {
    return or_transformer<BitFieldsT>{to_or | other.to_or};
  }
};

// For building atomic updates affecting one or more fields, assuming all the
// updates are bitwise-and.
template <typename BitFieldsT>
struct and_transformer {
  using underlying_type = typename BitFieldsT::underlying_type;
  underlying_type to_and = 0;
  // + for general combine
  and_transformer<BitFieldsT> operator+(
      const and_transformer<BitFieldsT>& other) const {
    return and_transformer<BitFieldsT>{to_and & other.to_and};
  }
};

// Can represent a combination of both subtractions and additions, representing
// subtractions as the addition of a negated value. To ensure we don't create a
// net overflow or underflow between fields, in debug builds we track the
// corresponding preconditions. (NOTE that when representing a subtraction, we
// rely on overflow of the unsigned representation.)
template <typename BitFieldsT>
struct add_transformer {
  using underlying_type = typename BitFieldsT::underlying_type;
  underlying_type to_add = 0;
#ifndef NDEBUG
  struct precondition {
    underlying_type mask; // for bits of the target field
    underlying_type piece; // component of to_add for the target field
  };
  std::vector<precondition> preconditions;
#endif // NDEBUG

  add_transformer() = default;
  explicit add_transformer(underlying_type val) : to_add(val) {}

  void assertPreconditions([[maybe_unused]] underlying_type from) const {
#ifndef NDEBUG
    for (auto p : preconditions) {
      underlying_type tmp = (from & p.mask) + p.piece;
      // Assert no under/overflow (unless the field is at the top bits of the
      // representation in underlying_type, which is allowed because it doesn't
      // lead to leakage into other fields)
      assert((tmp & ~p.mask) == 0);
    }
#endif // NDEBUG
  }

  // + for general combine
  add_transformer<BitFieldsT> operator+(
      const add_transformer<BitFieldsT>& other) const {
    add_transformer<BitFieldsT> rv(to_add + other.to_add);
#ifndef NDEBUG
    rv.preconditions = preconditions;
    rv.preconditions.insert(
        rv.preconditions.end(),
        other.preconditions.begin(),
        other.preconditions.end());
#endif // NDEBUG
    return rv;
  }
};

namespace detail {

// NOTE: PrevField is not a direct template parameter here to avoid exponential
// blowup in compiled mangled names
template <typename BitFieldsT, int PrevFieldEndBit>
struct bool_bit_field_impl {
  using parent_type = BitFieldsT;
  using parent_base = bit_fields<
      typename BitFieldsT::underlying_type,
      typename BitFieldsT::derived_type>;
  using underlying_type = typename BitFieldsT::underlying_type;
  using value_type = bool;
  static constexpr int kBitOffset = PrevFieldEndBit;
  static constexpr int kEndBit = kBitOffset + 1;
  static_assert(kBitOffset >= 0 && kEndBit <= BitFieldsT::kBitCount);

  // no instances
  bool_bit_field_impl() = delete;

  // NOTE: allow BitFieldsT to be derived from bit_fields<> which can be
  // passed in here
  static constexpr bool getFrom(const parent_base& bf) {
    return (bf.underlying & (underlying_type{1} << kBitOffset)) != 0;
  }
  static constexpr void setIn(parent_base& bf, bool value) {
    // NOTE: avoiding conditional branches is usually best for speed on modern
    // processors
    bf.underlying = (bf.underlying & ~(underlying_type{1} << kBitOffset)) |
        (underlying_type{value} << kBitOffset);
  }
  static or_transformer<BitFieldsT> setTransform() { return orTransform(true); }
  static or_transformer<BitFieldsT> orTransform(bool b) {
    return or_transformer<BitFieldsT>{underlying_type{b} << kBitOffset};
  }
  static and_transformer<BitFieldsT> clearTransform() {
    return andTransform(false);
  }
  static and_transformer<BitFieldsT> andTransform(bool b) {
    return and_transformer<BitFieldsT>{~(underlying_type{!b} << kBitOffset)};
  }
};

// NOTE: PrevField is not a direct template parameter here to avoid exponential
// blowup in compiled mangled names
template <typename BitFieldsT, int kBitCount_, int PrevFieldEndBit>
struct unsigned_bit_field_impl {
  using parent_type = BitFieldsT;
  using underlying_type = typename BitFieldsT::underlying_type;
  // Smallest uint type that can fit kBitCount bits
  using value_type = std::conditional_t<
      kBitCount_ <= 8,
      uint8_t,
      std::conditional_t<
          kBitCount_ <= 16,
          uint16_t,
          std::conditional_t<kBitCount_ <= 32, uint32_t, uint64_t>>>;
  static constexpr int kBitOffset = PrevFieldEndBit;
  static constexpr int kBitCount = kBitCount_;
  static constexpr int kEndBit = kBitOffset + kBitCount;
  static_assert(kBitCount >= 1);
  static_assert(kBitCount <= 64);
  static_assert(kBitOffset >= 0 && kEndBit <= BitFieldsT::kBitCount);
  static constexpr bool kIncludesTopBit = (kEndBit == BitFieldsT::kBitCount);

  static constexpr value_type kMask =
      (value_type{1} << (kBitCount - 1) << 1) - 1;

  // no instances
  unsigned_bit_field_impl() = delete;

  static constexpr value_type getFrom(const BitFieldsT& bf) {
    return static_cast<value_type>((bf.underlying >> kBitOffset) & kMask);
  }

  static constexpr void setIn(BitFieldsT& bf, value_type value) {
    bf.underlying &= ~(static_cast<underlying_type>(kMask) << kBitOffset);
    bf.underlying |= static_cast<underlying_type>(value & kMask) << kBitOffset;
  }

  // Create a transform for clearing this field to zero.
  static and_transformer<BitFieldsT> clearTransform() {
    return and_transformer<BitFieldsT>{
        ~(static_cast<underlying_type>(kMask) << kBitOffset)};
  }

  // Create a transform for bitwise-and
  static and_transformer<BitFieldsT> andTransform(value_type value) {
    assert((value & ~kMask) == 0);
    return and_transformer<BitFieldsT>{
        ~(static_cast<underlying_type>(value ^ kMask) << kBitOffset)};
  }

  // Create a transform for bitwise-or
  static or_transformer<BitFieldsT> orTransform(value_type value) {
    assert((value & ~kMask) == 0);
    return or_transformer<BitFieldsT>{
        static_cast<underlying_type>(value) << kBitOffset};
  }

  // Create a transform for adding a particular value, but with the
  // precondition that adding the value will not overflow the field. This
  // applies for fields that do not include the top bit of the underlying
  // representation. Can be combined with other additive transforms for other
  // fields.
  static add_transformer<BitFieldsT> plusTransformPromiseNoOverflow(
      value_type value) {
    static_assert(!kIncludesTopBit);
    add_transformer<BitFieldsT> rv{
        static_cast<underlying_type>(value) << kBitOffset};
#ifndef NDEBUG
    rv.preconditions.push_back(
        {static_cast<underlying_type>(kMask) << kBitOffset, rv.to_add});
#endif // NDEBUG
    return rv;
  }

  // Create a transform for adding a particular value, but ignoring any
  // overflow in that field. This applies for fields that include the top bit
  // of the underlying representation. Can be combined with other additive
  // transforms for other fields.
  static add_transformer<BitFieldsT> plusTransformIgnoreOverflow(
      value_type value) {
    static_assert(kIncludesTopBit);
    add_transformer<BitFieldsT> rv{
        static_cast<underlying_type>(value) << kBitOffset};
    return rv;
  }

  // Create a transform for subtracting a particular value, but with the
  // precondition that subtracting the value will not underflow the field. This
  // applies for fields that do not include the top bit of the underlying
  // representation. Can be combined with other additive transforms for other
  // fields.
  static add_transformer<BitFieldsT> minusTransformPromiseNoUnderflow(
      value_type value) {
    static_assert(!kIncludesTopBit);
    add_transformer<BitFieldsT> rv{
        underlying_type{0} -
        (static_cast<underlying_type>(value) << kBitOffset)};
#ifndef NDEBUG
    rv.preconditions.push_back(
        {static_cast<underlying_type>(kMask) << kBitOffset, rv.to_add});
#endif // NDEBUG
    return rv;
  }

  // Create a transform for subtracting a particular value, but ignoring any
  // underflow in that field. This applies for fields that include the top bit
  // of the underlying representation. Can be combined with other additive
  // transforms for other fields.
  static add_transformer<BitFieldsT> minusTransformIgnoreUnderflow(
      value_type value) {
    static_assert(kIncludesTopBit);
    add_transformer<BitFieldsT> rv{
        underlying_type{0} -
        (static_cast<underlying_type>(value) << kBitOffset)};
    return rv;
  }
};

} // namespace detail

// Placeholder for PrevField for the first field
struct no_prev_bit_field {
  // no instances
  no_prev_bit_field() = delete;
  static constexpr int kEndBit = 0;
};

// For declaring a single-bit field accessed as a boolean. See example above on
// bit_fields
template <typename BitFieldsT, typename PrevField>
using bool_bit_field =
    detail::bool_bit_field_impl<BitFieldsT, PrevField::kEndBit>;

// For declaring a multi-bit field accessed as an unsigned int. See example
// above on bit_fields
template <typename BitFieldsT, int kBitCount, typename PrevField>
using unsigned_bit_field =
    detail::unsigned_bit_field_impl<BitFieldsT, kBitCount, PrevField::kEndBit>;

} // namespace folly
