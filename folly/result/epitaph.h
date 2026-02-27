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

#include <cstddef>
#include <cstdint>
#include <memory>

#include <folly/CppAttributes.h>
#include <folly/Portability.h> // FOLLY_HAS_RESULT
#include <folly/debugging/symbolizer/StackTrace.h>
#include <folly/lang/SafeAssert.h>
#include <folly/result/result.h>
#include <folly/result/rich_error.h>
#include <folly/result/rich_exception_ptr.h>
#include <folly/result/rich_msg.h>

#if FOLLY_HAS_RESULT

namespace folly {

namespace detail {

// -- Location storage policies for epitaph_impl --

// Source location (the regular epitaph case).
struct epitaph_source_location {
  static constexpr bool has_stack = false;
  source_location loc_{};

  epitaph_source_location() = default;
  explicit epitaph_source_location(source_location loc)
      : loc_{std::move(loc)} {} // NOLINT(performance-move-const-arg)
};

// Options for stack_epitaph's inline frame storage.
//
// Usage:
//   stack_epitaph(eos)                          // default (256 max, 17 inline)
//   stack_epitaph<{.max_frames = 64}>(eos)      // custom
//   stack_epitaph<{.max_frames = 64, .inline_frames = 8}>(eos)  // spill
//
// When `inline_frames < max_frames`, frames beyond the inline capacity
// spill to a ref-counted heap allocation, keeping the inline object small.
struct stack_epitaph_opts {
  size_t max_frames = 256;
  // Frames stored inline.  When `inline_frames < max_frames`, excess
  // frames spill to a ref-counted heap allocation.
  size_t inline_frames = 17;
  // Capture buffer for `getStackTrace()`: `max_frames` visible frames
  // plus 2 internal frames (`getStackTrace` + caller) that are skipped.
  constexpr size_t buffer_size() const { return max_frames + 2; }
};

// Inline stack frames captured via getStackTrace.  Zero-terminated.
// When `Opt.inline_frames < Opt.max_frames`, frames that exceed the
// inline capacity are stored in a ref-counted heap allocation.  The
// initial frames always go inline; only the tail spills to the heap.
template <stack_epitaph_opts Opt>
struct epitaph_stack_location {
  static constexpr bool has_stack = true;
  static constexpr size_t kInline = std::min(Opt.inline_frames, Opt.max_frames);
  static constexpr bool kFramesMayUseHeap = kInline < Opt.max_frames;

  // Zero-terminated: frames_[i] == 0 marks end.  Valid IPs are never 0.
  uintptr_t frames_[kInline + 1]{};

  // Heap storage for the tail beyond inline capacity.  Zero-terminated.
  struct no_heap_ {};
  [[FOLLY_ATTR_NO_UNIQUE_ADDRESS]]
  std::conditional_t<kFramesMayUseHeap, std::shared_ptr<uintptr_t[]>, no_heap_>
      heap_{};

  epitaph_stack_location() = default;
  epitaph_stack_location(const uintptr_t* addrs, size_t count) {
    auto n = std::min(count, Opt.max_frames);
    auto inline_n = std::min(n, kInline);
    std::copy(addrs, addrs + inline_n, frames_);
    frames_[inline_n] = 0;
    if constexpr (kFramesMayUseHeap) {
      if (n > kInline) {
        auto heap_n = n - kInline;
        heap_ = std::shared_ptr<uintptr_t[]>(new uintptr_t[heap_n + 1]());
        std::copy(addrs + kInline, addrs + n, heap_.get());
        heap_[heap_n] = 0;
      }
    }
  }
};

// Symbolize and format a zero-terminated stack frame array.
// `heap_frames` is the spilled tail (nullptr if all frames are inline).
void format_epitaph_stack(
    fmt::appender& out,
    const char* msg,
    const uintptr_t* frames,
    const uintptr_t* heap_frames = nullptr);

// Common epitaph implementation parameterized on location storage.
//
//   `epitaph_source_location`: regular epitaph (message + `source_location`)
//   `epitaph_stack_location<Opt>`: stack epitaph (message + inline frames)
//
// IMPORTANT: Epitaphs should NEVER support adding codes.  Codes often direct
// program control flow, whereas epitaphs are inherently discardable.  There
// is no way to guarantee that codes added via epitaphs would not be
// accidentally discarded (by throwing, by using `exception_ptr`, or by
// deliberately accessing the underlying error).
template <typename Location>
class epitaph_impl : public rich_error_base {
 private:
  rich_exception_ptr next_;
  exception_shared_string msg_;
  [[FOLLY_ATTR_NO_UNIQUE_ADDRESS]] Location location_;

  void setup_underlying() {
    // An empty `next_` suggests user error — normal `result` usage never
    // creates empty exception pointers.
    FOLLY_SAFE_DCHECK(next_ != rich_exception_ptr{});
    if (auto* rex = next_.get_outer_exception<rich_error_base>()) {
      underlying_error_private_t priv;
      if (auto* underlying = rex->mutable_underlying_error(priv)) {
        set_underlying_error(priv, underlying);
        return;
      }
    }
    set_underlying_error(underlying_error_private_t{}, &next_);
  }

  void setup_underlying_on_copy(const epitaph_impl& other) {
    FOLLY_SAFE_DCHECK(other.underlying_error());
    if (other.underlying_error() == &other.next_) {
      set_underlying_error(underlying_error_private_t{}, &next_);
    } else if (auto* next_rex = next_.get_outer_exception<rich_error_base>()) {
      // If "underlying" doesn't point at `next_`, it points at something
      // **owned** by `next_`, which means an address can only be safely taken
      // after we copy / move it in.
      //
      // It is tempting to try to micro-optimize this by making "copy" act as
      // "move" for this class, but that would break badly if someone threw an
      // error with epitaphs -- copies during throw are quite common.  Less
      // crucially, we would have to assume that "errors owned by `next_`" are
      // address-stable, though today this is indeed guaranteed.
      //
      // NB: This has to use `mutable_...` to let `rich_exception_ptr` provide
      // `get_mutable_exception`, but per the doc on that `rich_error_base`
      // API, we must be careful not to mutate the underlying REP object.
      underlying_error_private_t priv;
      set_underlying_error(priv, next_rex->mutable_underlying_error(priv));
    }
  }

 public:
  ~epitaph_impl() override = default;
  // IMPORTANT: Custom ctors are required because `underlying_ptr_` points into
  // `this` or data-owned-by-`this`, and thus must be relocated each time.
  epitaph_impl(const epitaph_impl& other)
      : next_{other.next_}, msg_{other.msg_}, location_{other.location_} {
    setup_underlying_on_copy(other);
  }
  epitaph_impl(epitaph_impl&& other) noexcept
      : next_{std::move(other.next_)},
        msg_{std::move(other.msg_)},
        location_{std::move(other.location_)} {
    setup_underlying_on_copy(other);
  }
  epitaph_impl& operator=(const epitaph_impl&) = delete;
  epitaph_impl& operator=(epitaph_impl&&) = delete;

  explicit epitaph_impl(rich_exception_ptr&& next, rich_msg msg)
    requires(!Location::has_stack)
      : next_{std::move(next)},
        msg_{std::move(msg).shared_string()},
        // NOLINTNEXTLINE(bugprone-use-after-move)
        location_{msg.location()} {
    setup_underlying();
  }

  explicit epitaph_impl(
      rich_exception_ptr&& next,
      exception_shared_string msg,
      const uintptr_t* addrs,
      size_t count)
    requires(Location::has_stack)
      : next_{std::move(next)}, msg_{std::move(msg)}, location_{addrs, count} {
    setup_underlying();
  }

  folly::source_location source_location() const noexcept override {
    if constexpr (Location::has_stack) {
      return {};
    } else {
      return location_.loc_;
    }
  }

  const char* partial_message() const noexcept override { return msg_.what(); }

  const rich_exception_ptr* next_error_for_epitaph() const noexcept override {
    return &next_;
  }

  void format_to(fmt::appender& out) const override {
    if constexpr (Location::has_stack) {
      const uintptr_t* heap = nullptr;
      if constexpr (Location::kFramesMayUseHeap) {
        if (location_.heap_) {
          heap = location_.heap_.get();
        }
      }
      format_epitaph_stack(out, msg_.what(), location_.frames_, heap);
    } else {
      rich_error_base::format_to(out);
    }
  }

  using folly_get_exception_hint_types = rich_error_hints<epitaph_impl>;
};

using epitaph_non_value = epitaph_impl<epitaph_source_location>;

// Wrap an error_or_stopped with a stack epitaph from already-captured frames.
template <stack_epitaph_opts Opt>
error_or_stopped make_stack_epitaph(
    error_or_stopped eos,
    exception_shared_string msg,
    const uintptr_t* addrs,
    ssize_t n) {
  auto count = static_cast<size_t>(std::max(ssize_t{0}, n));
  // Skip `getStackTrace` (1) + its direct caller (1).
  auto skip = std::min(size_t{2}, count);
  return error_or_stopped{rich_error<epitaph_impl<epitaph_stack_location<Opt>>>{
      std::move(eos).release_rich_exception_ptr(),
      std::move(msg),
      addrs + skip,
      count - skip}};
}

} // namespace detail

/// epitaph
///
/// You can add epitaphs to errors in `result` & `error_or_stopped` with
/// allocation-free literals, or via `fmt::format`), and source locations:
///
///   r = epitaph(my_result()) // only the source location
///   r = epitaph(my_result(), "ctx") // location & string literal
///   r = epitaph(my_result(), "fmt {}", a) // formatted, on heap
///
/// This takes ownership of the 1st argument, and returns a same-type value.
///
/// Thread-safety: The underlying exception MAY BE MUTATED, if it derives from
/// `rich_error_base`.  Do NOT allow concurrent access to exception objects!
///
/// If the input is in a value state, it is not changed.  Non-value states (both
/// error & stopped) are annotated with epitaphs (current location & message).
///
/// Crucially, `epitaph` never changes the type nor the
/// `get_rich_error_code()`s of the error -- access to both will work the same
/// as before adding epitaphs.  So, this works, as do `has_stopped()` checks:
///
///   eos = epitaph(error_or_stopped{std::logic_error{"BUG"}}, "AT");
///   if (auto ex = get_exception<std::logic_error>()) { // NOT `auto*`!
///     LOG(INFO) << ex; // Prints: AT @ src.cpp:42 -> BUG
///   }
///
/// For a wrapper that can change codes, check out `nestable_coded_rich_error`.
///
/// How epitaphs work under the hood:
///
///   - The inner `logic_error` is wrapped with a `rich_error` , but this is
///     not observable via APIs besides `get_outer_exception()`.  Seek `result`
///     maintainer advice before using this function!
///
///   - For normal error access, our `get_exception` implementation returns a
///     special `rich_ptr_to_underlying_error` that quacks like a pointer to
///     the underlying error, but prints the epitaph stack when formatted.
///
/// On the "value" path, the perf cost is minimal -- 1 branch.  On the "error"
/// path, adding epitaphs **may** allocate a new `std::exception_ptr` (now
/// 60ns for ctor + dtor, could use some micro-optimization), but given demand
/// we can amortize this to 5ns per call.
///
/// Epitaphs work regardless of the underlying exception type. Epitaph
/// data are exposed in an RTTI-free way via `rich_error_base`, so non-rich
/// errors are necessarily wrapped.
///
/// Future: `epitaphs.md` has pointers on how the epitaphs support
/// should evolve (for better perf & usability).
///
/// Does not throw when no format args are used (literal or empty message).
/// Else, `fmt` may throw `bad_alloc` -- but not `make_exception_ptr_with`.
template <typename... Args>
error_or_stopped epitaph(
    error_or_stopped eos,
    // The `format_string_and_location` doc explains the `type_identity`
    ext::format_string_and_location<std::type_identity_t<Args>...> snl = "",
    Args const&... args) {
  return error_or_stopped{rich_error<detail::epitaph_non_value>{
      std::move(eos).release_rich_exception_ptr(),
      rich_msg{std::move(snl), args...}}};
}

template <typename T, typename... Args>
result<T> epitaph(
    result<T> r,
    // The `error_or_stopped` overload explains the `type_identity`.
    ext::format_string_and_location<std::type_identity_t<Args>...> snl = "",
    Args const&... args) {
  if (r.has_value()) {
    return r;
  }
  return epitaph(std::move(r).error_or_stopped(), std::move(snl), args...);
}

/// stack_epitaph
///
/// Captures the current call stack (as raw instruction pointers) and wraps an
/// error with a stack-trace epitaph.  Symbolization is deferred to format
/// time, so the capture cost is just a libunwind walk.
///
/// Cost: ~10ns per stack frame for the libunwind walk.  At typical depths (~20
/// frames), ~200ns — negligible vs throw+catch (~2us).  Uses ~2KB of transient
/// stack space (default `max_frames=256`).
///
/// Memory: 192 bytes (3 cache lines, +8B on Windows) per epitaph besides
/// overhead.  First 17 frames stored inline; overflow spills to a ref-counted
/// heap allocation.
///
/// Semantics (same as regular `epitaph()`):
///   - May allocate a new `std::exception_ptr`.
///   - Discarded when the error is re-thrown or converted to plain eptr.
///   - Transparent: `get_exception<T>()` and `get_rich_error_code()` see
///     through epitaphs to the underlying error.
///   - Value inputs are returned unchanged (1 branch).
///
/// Used automatically by `result_promise::unhandled_exception()`, and also
/// available for user `catch` clauses:
///
///   } catch (...) {
///     co_return stack_epitaph(
///         error_or_stopped::from_current_exception(), "context msg");
///   }
///
/// Non-throwing when both are true:
///   - No format args are used (literal or empty message), AND
///   - Captured frames fit inline:
///       min(count, Opt.max_frames) <= Opt.inline_frames
/// Conversely: `fmt` string allocation may throw.  When frames exceed inline
/// capacity, the heap spill allocation may throw.
template <
    detail::stack_epitaph_opts Opt = detail::stack_epitaph_opts{},
    typename... Args>
FOLLY_NOINLINE error_or_stopped stack_epitaph(
    error_or_stopped eos,
    ext::format_string_and_location<std::type_identity_t<Args>...> snl = "",
    Args const&... args) {
  uintptr_t addrs[Opt.buffer_size()];
  auto n = symbolizer::getStackTrace(addrs, Opt.buffer_size());
  return detail::make_stack_epitaph<Opt>(
      std::move(eos), snl.as_exception_shared_string(args...), addrs, n);
}

template <
    detail::stack_epitaph_opts Opt = detail::stack_epitaph_opts{},
    typename T,
    typename... Args>
FOLLY_NOINLINE result<T> stack_epitaph(
    result<T> r,
    ext::format_string_and_location<std::type_identity_t<Args>...> snl = "",
    Args const&... args) {
  if (r.has_value()) {
    return r;
  }
  uintptr_t addrs[Opt.buffer_size()];
  auto n = symbolizer::getStackTrace(addrs, Opt.buffer_size());
  return detail::make_stack_epitaph<Opt>(
      std::move(r).error_or_stopped(),
      snl.as_exception_shared_string(args...),
      addrs,
      n);
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
