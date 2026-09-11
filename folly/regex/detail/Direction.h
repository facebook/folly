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
#include <string_view>

namespace folly::regex::detail {

enum class Direction { Forward, Reverse };

// Direction-aware view into the input string. Stores the complete original
// input and an active region within it. The active region is determined by
// the current direction's prefix strip:
//   Forward: prefix stripped from the left  (active_begin = prefix.size())
//   Reverse: prefix stripped from the right (active_end -= prefix.size())
//
// All engine positions are relative to the active region. The original_input
// is accessible for dot-star extension scanning. Probes that need full-input
// access use fullInput<Dir>() to get an unstripped view.
template <Direction Dir>
struct InputView {
  // Complete input string. Public for use by computeDotStarExtension and
  // computeLeadingDotStarExtension free functions in Executor.h.
  std::string_view original_input;

 private:
  std::size_t active_begin_ = 0;
  std::size_t active_end_ = 0;

 public:
  // Full input is active — no prefix strip.
  explicit InputView(std::string_view input) noexcept
      : original_input(input), active_begin_(0), active_end_(input.size()) {}

  // Prefix-stripped construction. The prefix is the verified literal prefix
  // of the current direction. Only prefix.size() is used — the bytes
  // themselves were already verified by memcmp in HybridMatcher.
  //   Forward: strips prefix.size() from the left
  //   Reverse: strips prefix.size() from the right
  InputView(std::string_view input, std::string_view prefix) noexcept
      : original_input(input) {
    if constexpr (Dir == Direction::Forward) {
      active_begin_ = prefix.size();
      active_end_ = input.size();
    } else {
      active_begin_ = 0;
      active_end_ = input.size() - prefix.size();
    }
  }

  std::size_t size() const noexcept { return active_end_ - active_begin_; }

  char operator[](std::size_t pos) const noexcept {
    return original_input[active_begin_ + pos];
  }

  static constexpr std::size_t advance(std::size_t pos) noexcept {
    if constexpr (Dir == Direction::Forward) {
      return pos + 1;
    } else {
      return pos - 1;
    }
  }

  bool canConsume(std::size_t pos) const noexcept {
    if constexpr (Dir == Direction::Forward) {
      return pos < size();
    } else {
      return pos > 0;
    }
  }

  bool atEnd(std::size_t pos) const noexcept { return !canConsume(pos); }

  std::size_t startPos() const noexcept {
    if constexpr (Dir == Direction::Forward) {
      return 0;
    } else {
      return size();
    }
  }

  char charAt(std::size_t pos) const noexcept {
    if constexpr (Dir == Direction::Forward) {
      return (*this)[pos];
    } else {
      return (*this)[pos - 1];
    }
  }

  std::size_t scanStart() const noexcept {
    if constexpr (Dir == Direction::Forward) {
      return 0;
    } else {
      return size();
    }
  }

  // Returns true when the scan position has gone past the valid range.
  // For forward: past size(). For reverse: wrapped past 0 (> size()).
  bool scanDone(std::size_t pos) const noexcept { return pos > size(); }

  // --- Position translation ---

  // Translate a position in this view's coordinate system to the
  // coordinate system of fullInput(). This is the sole position-translation
  // primitive. Used by probe evaluation code and dot-star free functions.
  std::size_t posInFull(std::size_t relPos) const noexcept {
    return active_begin_ + relPos;
  }

  // --- Direction switching ---

  // Create a view in the opposite direction. Resets the active region:
  // the current direction's prefix strip is removed (the forward prefix
  // is the reverse suffix — we can't strip a suffix), and the new
  // direction's prefix is applied fresh.
  //
  // Forward→Reverse: active_begin=0, active_end=size-other_prefix.size()
  // Reverse→Forward: active_begin=other_prefix.size(), active_end=size
  auto flip(std::string_view other_prefix = {}) const noexcept {
    constexpr auto OtherDir =
        Dir == Direction::Forward ? Direction::Reverse : Direction::Forward;
    InputView<OtherDir> result{original_input};
    if constexpr (Dir == Direction::Forward) {
      // Flipping to Reverse: remove forward left-strip, apply reverse
      // right-strip from other_prefix.
      result.active_begin_ = 0;
      result.active_end_ = original_input.size() - other_prefix.size();
    } else {
      // Flipping to Reverse→Forward: remove reverse right-strip, apply
      // forward left-strip from other_prefix.
      result.active_begin_ = other_prefix.size();
      result.active_end_ = original_input.size();
    }
    return result;
  }

  // --- Full-input access for probes ---

  // Returns an InputView over the entire original_input in the requested
  // direction. Used by lookbehind probes (Reverse), lookahead probes
  // (Forward), and possessive probes (Forward).
  template <Direction D = Dir>
  InputView<D> fullInput() const noexcept {
    return InputView<D>{original_input};
  }

  // --- Narrowing ---

  // Narrow the active region to [rel_begin, rel_end) relative to the
  // current active region. original_input is preserved, so fullInput()
  // on a narrowed view still returns the complete original string.
  InputView narrowTo(
      std::size_t rel_begin, std::size_t rel_end) const noexcept {
    InputView result{original_input};
    result.active_begin_ = active_begin_ + rel_begin;
    result.active_end_ = active_begin_ + rel_end;
    return result;
  }

  // Grant access to active_begin_/active_end_ for flip() and narrowTo()
  // across different InputView<Dir> instantiations.
  template <Direction>
  friend struct InputView;
};

} // namespace folly::regex::detail
