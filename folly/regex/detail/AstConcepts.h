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

#include <concepts>
#include <string_view>
#include <type_traits>

#include <folly/regex/detail/Ast.h>

namespace folly::regex::detail {

// ReadOnlyAst: concept for read-only access to a parsed AST.
// Satisfied by ParseResult and const AstBuilder.
// NOT required by ProbeStore (which has a narrower interface).
template <typename T>
concept ReadOnlyAst = requires(const T& ast, int idx, char c) {
  // Data arrays
  { ast.nodes[idx] } -> std::convertible_to<const AstNode&>;
  { ast.node_count } -> std::convertible_to<int>;
  { ast.char_classes[idx] } -> std::convertible_to<const CharClassEntry&>;
  { ast.char_class_count } -> std::convertible_to<int>;
  { ast.ranges[idx] } -> std::convertible_to<const CharRange&>;
  { ast.total_ranges } -> std::convertible_to<int>;
  { ast.root } -> std::convertible_to<NodeIdx>;

  // Methods
  { ast.charClassTestAt(idx, c) } -> std::convertible_to<bool>;
  { ast.literal_prefix() } -> std::convertible_to<std::string_view>;

  // AST metadata
  { ast.valid } -> std::convertible_to<bool>;
  { ast.nfa_compatible } -> std::convertible_to<bool>;
  { ast.has_lookbehind } -> std::convertible_to<bool>;
  { ast.min_width } -> std::convertible_to<int>;
  { ast.probe_count } -> std::convertible_to<int>;
  { ast.group_count } -> std::convertible_to<int>;
  { ast.backtrack_safe } -> std::convertible_to<bool>;
  { ast.has_backref } -> std::convertible_to<bool>;
  { ast.prefix_len } -> std::convertible_to<int>;
  { ast.prefix_strip_len } -> std::convertible_to<int>;
  { ast.trailing_dot_star_min } -> std::convertible_to<int>;
  { ast.trailing_dot_star_dot_all } -> std::convertible_to<bool>;
  { ast.trailing_dot_star_anchor } -> std::convertible_to<int>;
  { ast.leading_dot_star_min } -> std::convertible_to<int>;
  { ast.leading_dot_star_dot_all } -> std::convertible_to<bool>;
  { ast.leading_dot_star_anchor } -> std::convertible_to<int>;
  {
    ast.prefix_group_adjustments[idx]
  } -> std::convertible_to<const PrefixGroupAdjustment&>;
  { ast.prefix_group_adjustment_count } -> std::convertible_to<int>;
};

// MutableAst: concept for mutable AST access during parsing and optimization.
// Satisfied by AstBuilder. Refines ReadOnlyAst.
template <typename T>
concept MutableAst =
    ReadOnlyAst<T> &&
    requires(
        T& ast,
        int idx,
        AstNode node,
        const CharRangeSet& rs,
        const char* data,
        char c) {
      // Mutation methods
      { ast.addNode(node) } -> std::convertible_to<NodeIdx>;
      { ast.addCharClass(rs) } -> std::convertible_to<int>;
      { ast.appendLiteral(data, idx) } -> std::convertible_to<std::string_view>;
      { ast.appendLiteralChar(c) } -> std::convertible_to<std::string_view>;
      ast.markDead(idx);
      ast.markDeadRecursive(idx);
      ast.setError(static_cast<std::size_t>(0), data);
      ast.ensurePrefixGroupAdjustments();

      // Mutable data access
      { ast.nodes[idx] } -> std::convertible_to<AstNode&>;
      requires std::is_assignable_v<decltype((ast.root)), NodeIdx>;
    };

// ---------------------------------------------------------------------------
// Compile-time concept verification. These static_asserts verify that the
// concepts correctly accept/reject the expected types.
// ---------------------------------------------------------------------------

// Use a minimal ParseSizes for the static_assert instantiation.
namespace concept_check_detail {
inline constexpr ParseSizes kMinSizes{1, 1, 1, 1, 1, 0, 0};
inline constexpr ProbeStoreSizes kMinProbeSizes{1, 1, 1, 1, 1};
} // namespace concept_check_detail

// ReadOnlyAst is satisfied by ParseResult (the immutable compiled form).
static_assert(ReadOnlyAst<ParseResult<concept_check_detail::kMinSizes>>);

// ReadOnlyAst is satisfied by const AstBuilder (read-only access).
static_assert(ReadOnlyAst<const AstBuilder>);

// ReadOnlyAst is NOT satisfied by ProbeStore (narrower interface —
// missing group_count, literal_prefix(), etc.).
static_assert(!ReadOnlyAst<ProbeStore<concept_check_detail::kMinProbeSizes>>);

// MutableAst is satisfied by AstBuilder (mutable access during parsing).
static_assert(MutableAst<AstBuilder>);

// MutableAst is NOT satisfied by ParseResult (immutable).
static_assert(!MutableAst<ParseResult<concept_check_detail::kMinSizes>>);

// MutableAst refines ReadOnlyAst: anything satisfying MutableAst also
// satisfies ReadOnlyAst.
static_assert(ReadOnlyAst<AstBuilder>);

} // namespace folly::regex::detail
