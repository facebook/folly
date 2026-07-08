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

/// Macros for forcing IEEE-precise floating-point semantics even when the
/// translation unit is compiled with -ffast-math or -Ofast.
///
/// FOLLY_FLOAT_PRECISE:
///   Scoped pragma for Clang. Should be placed at the start of the function
///   body, before any floating-point operations that need protection. Clang
///   scopes it to the enclosing braces, so no corresponding "pop" is needed.
///   No-op on GCC (which doesn't support float_control inside functions) and
///   on MSVC (whose float_control pragma is not brace-scoped and would leak
///   the precise setting into the rest of the translation unit).
///
/// FOLLY_FLOAT_PRECISE_BEGIN / FOLLY_FLOAT_PRECISE_END:
///   Paired guards for GCC. Place FOLLY_FLOAT_PRECISE_BEGIN immediately before
///   the function definition and FOLLY_FLOAT_PRECISE_END immediately after it.
///   No-op on Clang (which uses FOLLY_FLOAT_PRECISE instead) and on MSVC.
///
/// Usage:
///   FOLLY_FLOAT_PRECISE_BEGIN
///   void my_function() {
///     FOLLY_FLOAT_PRECISE
///     // IEEE-precise floating-point operations here
///   }
///   FOLLY_FLOAT_PRECISE_END

#if defined(__clang__)

// Clang: #pragma float_control(precise, on) at the start of a compound
// statement forces IEEE-precise semantics for the rest of that compound
// statement, even under -ffast-math. Clang scopes the setting to the
// enclosing braces and restores it at the closing brace. MSVC is
// deliberately excluded: its float_control(precise, on) (without a matching
// push/pop) persists to end of translation unit, so using it inside a header
// function would leak the precise-FP setting into every subsequent function
// in any TU that includes this header.
#define FOLLY_FLOAT_PRECISE _Pragma("float_control(precise, on)")
#define FOLLY_FLOAT_PRECISE_BEGIN
#define FOLLY_FLOAT_PRECISE_END

#elif defined(__GNUC__)

// GCC: bracket the function definition with the target-independent
// optimization pragmas. `#pragma GCC push_options` saves the current option
// state, `#pragma GCC optimize("no-fast-math")` disables fast-math
// optimizations for the function(s) that follow, and `#pragma GCC pop_options`
// restores the previous state so the setting does not leak into the rest of
// the translation unit. This documented Function-Specific Option Pragma route
// is more dependable than __attribute__((optimize(...))), which GCC treats as
// a debugging aid. GCC does not support float_control inside functions, and
// Clang silently ignores these pragmas, so these are mutually exclusive with
// FOLLY_FLOAT_PRECISE. The pragmas can still interact unpredictably with
// inlining, template instantiation, and LTO; when relying on them, verify with
// -fverbose-asm at the intended optimization level that the compensation step
// is actually preserved.
#define FOLLY_FLOAT_PRECISE
#define FOLLY_FLOAT_PRECISE_BEGIN \
  _Pragma("GCC push_options") _Pragma("GCC optimize(\"no-fast-math\")")
#define FOLLY_FLOAT_PRECISE_END _Pragma("GCC pop_options")

#else

#define FOLLY_FLOAT_PRECISE
#define FOLLY_FLOAT_PRECISE_BEGIN
#define FOLLY_FLOAT_PRECISE_END

#endif
