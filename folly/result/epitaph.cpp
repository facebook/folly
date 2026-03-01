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

#include <folly/result/epitaph.h>

#include <folly/Portability.h> // FOLLY_HAS_RESULT

// Enabled by default on platforms with ELF+DWARF; BUCK sets this to 0 on
// Android to cut the symbolizer link-time dep that disrupts APK SO-module
// packaging (T257592212).
#include <folly/portability/Config.h> // FOLLY_HAVE_ELF, FOLLY_HAVE_DWARF

#ifndef FOLLY_EPITAPH_USE_SYMBOLIZER
#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
#define FOLLY_EPITAPH_USE_SYMBOLIZER 1
#else
#define FOLLY_EPITAPH_USE_SYMBOLIZER 0
#endif
#endif

#if FOLLY_EPITAPH_USE_SYMBOLIZER
#include <folly/Demangle.h>
#include <folly/debugging/symbolizer/StackTrace.h>
#include <folly/debugging/symbolizer/Symbolizer.h>
#endif

#if FOLLY_HAS_RESULT

namespace folly::detail {

void format_epitaph_stack(
    fmt::appender& out,
    const char* msg,
    const uintptr_t* frames,
    const uintptr_t* heap_frames) {
  if (msg[0]) {
    fmt::format_to(out, "{} ", msg);
  }
  // Collect zero-terminated inline frames + optional heap tail.
  std::vector<uintptr_t> all;
  while (*frames) {
    all.push_back(*frames++);
  }
  if (heap_frames) {
    while (*heap_frames) {
      all.push_back(*heap_frames++);
    }
  }
  if (all.empty()) {
    fmt::format_to(out, "[stack: no frames]");
    return;
  }
  fmt::format_to(out, "[stack:");
#if FOLLY_EPITAPH_USE_SYMBOLIZER
  // Symbolize on demand — only paid at format/log time.
  folly::symbolizer::Symbolizer symbolizer;
  std::vector<folly::symbolizer::SymbolizedFrame> symFrames(all.size());
  symbolizer.symbolize(all.data(), symFrames.data(), all.size());
  for (size_t i = 0; i < all.size(); ++i) {
    auto& f = symFrames[i];
    if (f.found && f.name) {
      fmt::format_to(out, "\n  #{} {}", i, folly::demangle(f.name));
      if (f.location.hasFileAndLine) {
        fmt::format_to(
            out, " @ {}:{}", f.location.file.toString(), f.location.line);
      }
    } else {
      fmt::format_to(out, "\n  #{} {:#x}", i, all[i]);
    }
  }
#else
  for (size_t i = 0; i < all.size(); ++i) {
    fmt::format_to(out, "\n  #{} {:#x}", i, all[i]);
  }
#endif
  fmt::format_to(out, "]");
}

void stack_epitaph_for_unhandled_exception(error_or_stopped& eos) noexcept {
  // We want `unhandled_exception()` to be `noexcept`.  We could only throw
  // while allocating to spill frames to heap.  To avoid that, store all frames
  // inline via `max_frames == inline_frames`.  19 is often enough, and the
  // objects fit neatly in 3 cache lines (given lucky alignment).
  //
  // If you must support deeper stacks: set `inline_frames < max_frames`, and
  // use try/catch around the heap allocation — on failure, either truncate to
  // inline-only, or fall back to the bare exception without an epitaph.
  constexpr stack_epitaph_opts opts{.max_frames = 19, .inline_frames = 19};
  static_assert( // 3 cache lines; not checked on Windows (wider exception_ptr).
      sizeof(void*) != 8 || sizeof(rich_exception_ptr) != 8 ||
      sizeof(epitaph_impl<epitaph_stack_location<opts>>) == 192);
  uintptr_t addrs[opts.buffer_size()];
#if FOLLY_EPITAPH_USE_SYMBOLIZER
  auto n = symbolizer::getStackTrace(addrs, opts.buffer_size());
#else
  ssize_t n = 0;
#endif
  eos = make_stack_epitaph<opts>(
      error_or_stopped::from_current_exception(),
      exception_shared_string{literal_c_str{""}},
      addrs,
      n);
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
