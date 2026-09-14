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

#include <cstdint>

namespace folly::regex {

// Flags controls both runtime behavior and compile-time syntax selection.
//
// Three categories of bits:
//
//   Bits 0-9   Behavior flags — control execution semantics (e.g. Multiline,
//              DotAll, CaseInsensitive). Bits 0-2 (ForceBacktracking,
//              ForceNFA, ForceDFA) are execution-only and excluded from
//              kCompilationFlagMask.
//
//   Bits 16-31 Syntax_* construct-enable flags — one bit per gateable parser
//              construct. When set, the parser accepts the corresponding
//              syntax; when clear, the parser rejects it with a descriptive
//              error. These bits are included in kCompilationFlagMask because
//              different syntax sets produce different ASTs.
//
//   SyntaxPreset_* members are convenience combinations of Syntax_* bits
//              that model specific regex engine flavors. Each preset is a
//              positive enable-set: it lists the constructs the flavor
//              supports. Removing a construct is Preset & ~Syntax_Foo.
//              Adding one is Preset | Syntax_Foo.
//
// Syntax-mask-default rule: if a Flags value has NO Syntax_* bits set (e.g.
// Flags::None, or Flags::CaseInsensitive alone), compilationFlags() normalizes
// it to include SyntaxPreset_Folly so the full superset syntax is used. This
// preserves backward compatibility — existing callers that don't mention
// syntax flags get exactly today's behavior. Callers who set any Syntax_* or
// SyntaxPreset_* bits suppress the default and get their explicit syntax.
enum class Flags : uint64_t {
  None = 0,

  // --- Behavior flags (bits 0-9) ---
  ForceBacktracking = 1ULL << 0,
  ForceNFA = 1ULL << 1,
  ForceDFA = 1ULL << 2,
  Multiline = 1ULL << 3,
  DotAll = 1ULL << 4,
  ForceReverseExecution = 1ULL << 5,
  CaseInsensitive = 1ULL << 6,
  Ungreedy = 1ULL << 7,
  NoAutoCapture = 1ULL << 8,
  Extended = 1ULL << 9,

  // --- Syntax construct-enable flags (bits 16-31) ---
  Syntax_AtomicGroup = 1ULL << 16,
  Syntax_Lookaround = 1ULL << 17,
  Syntax_NamedGroupAngle = 1ULL << 18,
  Syntax_NamedGroupPython = 1ULL << 19,
  Syntax_InlineFlags = 1ULL << 20,
  Syntax_NumericBackref = 1ULL << 21,
  Syntax_GBackref = 1ULL << 22,
  Syntax_KBackref = 1ULL << 23,
  Syntax_ShorthandClasses = 1ULL << 24,
  Syntax_WordBoundary = 1ULL << 25,
  Syntax_BufferAnchors = 1ULL << 26,
  Syntax_SingleByteEscape = 1ULL << 27,
  Syntax_XBraceHex = 1ULL << 28,
  Syntax_PosixCharClass = 1ULL << 29,
  Syntax_PossessiveQuantifier = 1ULL << 30,
  Syntax_LazyQuantifier = 1ULL << 31,

  // --- Internal syntax flags (bits 55+) ---
  // Enables (?~...) annotation constructs used by the serializer to encode
  // metadata (group IDs, probe IDs, discriminator offsets) in the serialized
  // regex. Not user-facing; only used for the inner re-parse pipeline.
  Syntax_InternalSerialized = 1ULL << 55,

  // --- Syntax presets ---

  // Full superset: every Syntax_* flag enabled.
  SyntaxPreset_Folly = Syntax_AtomicGroup | Syntax_Lookaround |
      Syntax_NamedGroupAngle | Syntax_NamedGroupPython | Syntax_InlineFlags |
      Syntax_NumericBackref | Syntax_GBackref | Syntax_KBackref |
      Syntax_ShorthandClasses | Syntax_WordBoundary | Syntax_BufferAnchors |
      Syntax_SingleByteEscape | Syntax_XBraceHex | Syntax_PosixCharClass |
      Syntax_PossessiveQuantifier | Syntax_LazyQuantifier,

  // PCRE2: true superset of folly's current support.
  SyntaxPreset_Pcre = Syntax_AtomicGroup | Syntax_Lookaround |
      Syntax_NamedGroupAngle | Syntax_NamedGroupPython | Syntax_InlineFlags |
      Syntax_NumericBackref | Syntax_GBackref | Syntax_KBackref |
      Syntax_ShorthandClasses | Syntax_WordBoundary | Syntax_BufferAnchors |
      Syntax_SingleByteEscape | Syntax_XBraceHex | Syntax_PosixCharClass |
      Syntax_PossessiveQuantifier | Syntax_LazyQuantifier,

  // Ruby: drops NamedGroupPython and SingleByteEscape.
  SyntaxPreset_Ruby = Syntax_AtomicGroup | Syntax_Lookaround |
      Syntax_NamedGroupAngle | Syntax_InlineFlags | Syntax_NumericBackref |
      Syntax_GBackref | Syntax_KBackref | Syntax_ShorthandClasses |
      Syntax_WordBoundary | Syntax_BufferAnchors | Syntax_XBraceHex |
      Syntax_PosixCharClass | Syntax_PossessiveQuantifier |
      Syntax_LazyQuantifier,

  // Python re: drops Perl (?<>), \g{...}, \C, atomic, possessive, \x{},
  // POSIX classes.
  SyntaxPreset_Python = Syntax_Lookaround | Syntax_NamedGroupPython |
      Syntax_InlineFlags | Syntax_NumericBackref | Syntax_KBackref |
      Syntax_ShorthandClasses | Syntax_WordBoundary | Syntax_BufferAnchors |
      Syntax_LazyQuantifier,

  // RE2: linear-time-safe subset. No backrefs, lookaround, atomic, possessive.
  SyntaxPreset_Re2 = Syntax_NamedGroupAngle | Syntax_NamedGroupPython |
      Syntax_InlineFlags | Syntax_ShorthandClasses | Syntax_WordBoundary |
      Syntax_BufferAnchors | Syntax_XBraceHex | Syntax_PosixCharClass |
      Syntax_LazyQuantifier,

  // Serialized inner re-parse: only the Syntax_* flags the serializer emits,
  // plus Syntax_InternalSerialized to enable (?~...) annotations. NOT
  // user-facing — used only by AstHolderInner's re-parse pipeline.
  SyntaxPreset_Serialized = Syntax_Lookaround | Syntax_NamedGroupAngle |
      Syntax_InlineFlags | Syntax_NumericBackref | Syntax_SingleByteEscape |
      Syntax_WordBoundary | Syntax_BufferAnchors | Syntax_PossessiveQuantifier |
      Syntax_LazyQuantifier | Syntax_InternalSerialized,
};

constexpr Flags operator|(Flags a, Flags b) noexcept {
  return static_cast<Flags>(
      static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

constexpr Flags operator&(Flags a, Flags b) noexcept {
  return static_cast<Flags>(
      static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

constexpr Flags operator~(Flags a) noexcept {
  return static_cast<Flags>(~static_cast<uint64_t>(a));
}

constexpr bool hasFlag(Flags flags, Flags flag) noexcept {
  return (static_cast<uint64_t>(flags) & static_cast<uint64_t>(flag)) != 0;
}

// Mask of all Syntax_* construct-enable bits.
constexpr Flags kSyntaxFlagMask =
    Flags::SyntaxPreset_Folly | Flags::Syntax_InternalSerialized;

// Flags that affect AST/NFA/DFA compilation. Execution-only flags
// (ForceBacktracking, ForceNFA, ForceDFA) do not change compiled
// artifacts and are excluded so that different execution modes can
// share the same backing storage. Syntax_* bits are included because
// different syntax sets produce different ASTs.
constexpr Flags kCompilationFlagMask = Flags::Multiline | Flags::DotAll |
    Flags::CaseInsensitive | Flags::ForceReverseExecution | Flags::Ungreedy |
    Flags::NoAutoCapture | Flags::Extended | kSyntaxFlagMask;

// Strips execution-only flags, preserving behavior and syntax bits.
constexpr Flags compilationFlags(Flags f) noexcept {
  return f & kCompilationFlagMask;
}

// compilationFlags + syntax-mask-default: used by Regex<> to compute the
// template key for AstHolder/NfaHolder/DfaHolder so that Regex<Pat> and
// Regex<Pat, SyntaxPreset_Folly> share the same instantiations. The parser
// applies the same default internally via parseRegex(), so this is only
// needed for template deduplication, not for correctness.
constexpr Flags normalizedCompilationFlags(Flags f) noexcept {
  Flags cf = f & kCompilationFlagMask;
  if ((cf & kSyntaxFlagMask) == Flags::None) {
    cf = cf | Flags::SyntaxPreset_Folly;
  }
  return cf;
}

} // namespace folly::regex
