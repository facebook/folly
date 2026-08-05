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

#include <array>
#include <type_traits>

#include <folly/lang/Exception.h>

namespace folly::regex::detail {

// ---------------------------------------------------------------------------
// constexprSwitch — O(1) enum dispatch via compile-time function pointer table
// ---------------------------------------------------------------------------
//
// Switch statements in constexpr evaluation cost 1 step per case label
// checked. This helper converts a switch into an array-indexed dispatch,
// reducing O(N) case-label scanning to O(1) lookup.
//
// === Quick start ===
//
//   constexprSwitch<static_cast<int>(MyEnum::Last),
//       Case<MyEnum::A, [](int x) { return x + 1; }>,
//       Case<MyEnum::B, Fallthrough>,
//       Case<MyEnum::C, [](int x) { return x * 2; }>,
//       Case<Default, [](int x) { return -1; }>
//   >(enumVal, arg);
//
// === Creating a per-enum alias ===
//
// Each enum should have a templated using alias that fixes MaxEnumValue so
// call sites don't need to repeat it:
//
//   template <typename... Cases>
//   using MyEnumSwitch = ConstexprSwitchDispatcher<
//       static_cast<int>(MyEnum::LastValue), Cases...>;
//
// === Migrating a switch statement ===
//
// 1. If the switch body accesses more than 1 local variable, define a local
//    struct that bundles all needed state as members (references are fine).
//    Add helper methods (e.g., follow()) for common patterns. If only 1
//    argument is needed, pass it directly without a struct.
//
//      struct Ctx {
//        const Context& state;
//        int s;
//        Result& result;
//        // ... all locals the cases need ...
//        constexpr void follow(int val) { /* common pattern */ }
//      };
//      Ctx ctx{state, s, result, ...};
//
// 2. Convert each case label to a Case<> entry with a handler lambda that
//    takes `auto& c` (the context struct). The lambda is an NTTP — it must
//    not capture anything. It has access to the enclosing function's
//    template parameters (e.g., Dir) for use with if constexpr.
//
// 3. Group consecutive cases that share a handler using Fallthrough:
//
//      Case<MyEnum::A, Fallthrough>,
//      Case<MyEnum::B, Fallthrough>,
//      Case<MyEnum::C, handler>    // A, B, C all dispatch to handler
//
// 4. Use Default for a catch-all. If no Default is provided, every enum
//    value 0..MaxEnumValue (inclusive) must have an explicit Case (or
//    Fallthrough). Missing values produce a compile error.
//
// 5. Dispatch via the alias:
//
//      MyEnumSwitch<Case<...>, ...>::dispatch(enumVal, ctx);
//
// === Sentinel values ===
//
//   Fallthrough  — This case uses the handler of the next Case entry.
//                  Chains are supported (A→B→C all resolve to C's handler).
//   Default      — Used as the Key in a Case to provide a handler for all
//                  enum values not explicitly listed.
//
// === Constraints ===
//
// - Enum values must be contiguous starting from 0.
// - All handler lambdas must be stateless (no captures) and valid as NTTPs.
// - All handlers must share the same return type and parameter types.
// ---------------------------------------------------------------------------

// Sentinel types for constexprSwitch Case entries.
struct FallthroughTag {
  constexpr bool operator==(const FallthroughTag&) const = default;
};
struct DefaultTag {
  constexpr bool operator==(const DefaultTag&) const = default;
};

inline constexpr FallthroughTag Fallthrough{};
inline constexpr DefaultTag Default{};

// A single case entry mapping an enum value (or Default) to a handler lambda
// (or Fallthrough).
template <auto Key, auto Handler>
struct Case {};

namespace constexpr_switch_detail {

// Wrap a generic lambda NTTP into a concrete function pointer by
// instantiating its call operator with the given argument types.
template <auto Handler, typename RetType, typename... Args>
constexpr RetType wrappedCall(Args... args) {
  return Handler(static_cast<Args&&>(args)...);
}

// Extract key/handler from a Case type.
template <typename C>
struct CaseTraits;
template <auto K, auto H>
struct CaseTraits<Case<K, H>> {
  static constexpr auto key = K;
  static constexpr auto handler = H;
};

// Check if a value is one of the sentinel types.
template <auto V>
inline constexpr bool isFallthrough =
    std::is_same_v<std::remove_cv_t<decltype(V)>, FallthroughTag>;
template <auto V>
inline constexpr bool isDefault =
    std::is_same_v<std::remove_cv_t<decltype(V)>, DefaultTag>;

// Find the first non-Fallthrough handler in the Cases pack.
template <typename First, typename... Rest>
constexpr auto findFirstHandler() {
  if constexpr (isFallthrough<CaseTraits<First>::handler>) {
    return findFirstHandler<Rest...>();
  } else {
    return CaseTraits<First>::handler;
  }
}

// Get the integer key for a Case (-1 for Default).
template <typename C>
constexpr int getKey() {
  constexpr auto k = CaseTraits<C>::key;
  if constexpr (isDefault<k>) {
    return -1;
  } else {
    return static_cast<int>(k);
  }
}

// Get the function pointer for a Case (nullptr for Fallthrough).
template <typename C, typename RetType, typename... Args>
constexpr auto getHandlerFn() -> RetType (*)(Args...) {
  constexpr auto h = CaseTraits<C>::handler;
  if constexpr (isFallthrough<h>) {
    return nullptr;
  } else {
    return &wrappedCall<h, RetType, Args...>;
  }
}

// Build the dispatch table at compile time.
template <typename FnPtr, int TableSize, int NumCases>
constexpr auto buildTable(
    const int (&keys)[NumCases], const FnPtr (&handlers)[NumCases]) {
  std::array<FnPtr, TableSize> result{};

  // Pass 1: fill with default handler (if any).
  for (int i = 0; i < NumCases; ++i) {
    if (keys[i] == -1 && handlers[i] != nullptr) {
      for (int j = 0; j < TableSize; ++j) {
        result[j] = handlers[i];
      }
      break;
    }
  }

  // Pass 2: process cases in reverse to resolve Fallthrough chains.
  FnPtr current = nullptr;
  for (int i = NumCases - 1; i >= 0; --i) {
    if (keys[i] == -1) {
      continue; // skip Default
    }
    if (handlers[i] == nullptr) {
      result[keys[i]] = current;
    } else {
      current = handlers[i];
      result[keys[i]] = current;
    }
  }

  // Validate: every table slot must be filled.
  for (int i = 0; i < TableSize; ++i) {
    if (result[i] == nullptr) {
      folly::throw_exception<std::runtime_error>(
          "constexprSwitch: missing case for enum value");
    }
  }

  return result;
}

} // namespace constexpr_switch_detail

// See documentation at the top of this file for usage and migration guide.
template <int MaxEnumValue, typename... Cases>
struct ConstexprSwitchDispatcher {
  // Inner struct parameterized on the call-site argument types.
  // Static members are initialized once per instantiation.
  template <typename... Args>
  struct Table {
    static constexpr auto kFirstHandler =
        constexpr_switch_detail::findFirstHandler<Cases...>();
    using RetType = decltype(kFirstHandler(std::declval<Args>()...));
    using FnPtr = RetType (*)(Args...);
    static constexpr int kTableSize = MaxEnumValue + 1;
    static constexpr int kNumCases = sizeof...(Cases);

    static constexpr int kKeys[kNumCases] = {
        constexpr_switch_detail::getKey<Cases>()...};
    static constexpr FnPtr kHandlers[kNumCases] = {
        constexpr_switch_detail::getHandlerFn<Cases, RetType, Args...>()...};

    static constexpr auto kTable =
        constexpr_switch_detail::buildTable<FnPtr, kTableSize>(
            kKeys, kHandlers);
  };

  template <typename EnumT, typename... Args>
  static constexpr auto dispatch(EnumT enumVal, Args&&... args) {
    using T = Table<Args&&...>;
    return T::kTable[static_cast<int>(enumVal)](static_cast<Args&&>(args)...);
  }
};

// See documentation at the top of this file for usage and migration guide.
template <int MaxEnumValue, typename... Cases, typename EnumT, typename... Args>
constexpr auto constexprSwitch(EnumT enumVal, Args&&... args) {
  return ConstexprSwitchDispatcher<MaxEnumValue, Cases...>::dispatch(
      enumVal, static_cast<Args&&>(args)...);
}

} // namespace folly::regex::detail
