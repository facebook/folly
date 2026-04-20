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

namespace folly {
namespace regex {
namespace detail {

enum class Direction { Forward, Reverse };

// Direction-aware virtual input type. Stores the full untrimmed input and
// stripped-region metadata for both directions. Exposes an active region
// determined by the current direction's stripping, and absorbs all
// direction-dependent position/data methods.
template <Direction Dir>
struct InputView {
  std::string_view full_input;
  std::string_view fwd_left_strip;
  std::string_view fwd_right_strip;
  std::string_view rev_left_strip;
  std::string_view rev_right_strip;
  // Original complete input, preserved across substring extraction
  // for lookaround probe access. When empty, full_input is the outer input.
  std::string_view outer_input;
  // Offset of full_input within outer_input.
  std::size_t outer_offset = 0;

  // No-strip convenience: full input is active.
  explicit InputView(std::string_view input) noexcept : full_input(input) {}

  // Current direction's strips only. The other direction's strips are empty.
  InputView(
      std::string_view input,
      std::string_view left_strip,
      std::string_view right_strip) noexcept
      : full_input(input) {
    if constexpr (Dir == Direction::Forward) {
      fwd_left_strip = left_strip;
      fwd_right_strip = right_strip;
    } else {
      rev_left_strip = left_strip;
      rev_right_strip = right_strip;
    }
  }

  // Full constructor with all four strips.
  InputView(
      std::string_view input,
      std::string_view fwd_left,
      std::string_view fwd_right,
      std::string_view rev_left,
      std::string_view rev_right) noexcept
      : full_input(input),
        fwd_left_strip(fwd_left),
        fwd_right_strip(fwd_right),
        rev_left_strip(rev_left),
        rev_right_strip(rev_right) {}

  std::size_t activeStart() const noexcept {
    if constexpr (Dir == Direction::Forward) {
      return fwd_left_strip.size();
    } else {
      return rev_left_strip.size();
    }
  }

  std::size_t activeEnd() const noexcept {
    if constexpr (Dir == Direction::Forward) {
      return full_input.size() - fwd_right_strip.size();
    } else {
      return full_input.size() - rev_right_strip.size();
    }
  }

  std::size_t size() const noexcept { return activeEnd() - activeStart(); }

  char operator[](std::size_t pos) const noexcept {
    return full_input[activeStart() + pos];
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

  bool atEnd(std::size_t pos) const noexcept {
    if constexpr (Dir == Direction::Forward) {
      return pos >= size();
    } else {
      return pos == 0;
    }
  }

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

  // Access a character at an absolute position within the full input,
  // bypassing the active region offset. Used by fixed-width lookbehind
  // to access characters in stripped prefix/suffix regions.
  char fullCharAt(std::size_t absPos) const noexcept {
    return full_input[absPos];
  }

  // Get an InputView over the outer (original complete) input for
  // lookaround probe evaluation. Probes need to see beyond the active
  // region boundaries.
  InputView<Direction::Forward> probeInput() const noexcept {
    if (!outer_input.empty()) {
      return InputView<Direction::Forward>{outer_input};
    }
    return InputView<Direction::Forward>{full_input};
  }

  // Convert an active-region position to the outer input coordinate.
  std::size_t toOuterPos(std::size_t pos) const noexcept {
    return outer_offset + activeStart() + pos;
  }

  // Create a copy with outer_input set, for use when extracting
  // a substring for matchAnchored while preserving probe context.
  InputView withOuterInput(
      std::string_view oi, std::size_t offset) const noexcept {
    auto copy = *this;
    copy.outer_input = oi;
    copy.outer_offset = offset;
    return copy;
  }

  std::size_t scanStart() const noexcept {
    if constexpr (Dir == Direction::Forward) {
      return 0;
    } else {
      return size();
    }
  }

  // Returns true when the scan position has gone past the valid range.
  // For forward: past maxPos. For reverse: wrapped past 0 (> size()).
  bool scanDone(std::size_t pos) const noexcept { return pos > size(); }

  // Returns an InputView with the opposite direction. Current direction's
  // strips are preserved; the other direction's strips are filled from args.
  auto flip(
      std::string_view other_left_strip,
      std::string_view other_right_strip) const noexcept {
    constexpr auto OtherDir =
        Dir == Direction::Forward ? Direction::Reverse : Direction::Forward;
    if constexpr (Dir == Direction::Forward) {
      return InputView<OtherDir>(
          full_input,
          fwd_left_strip,
          fwd_right_strip,
          other_left_strip,
          other_right_strip);
    } else {
      return InputView<OtherDir>(
          full_input,
          other_left_strip,
          other_right_strip,
          rev_left_strip,
          rev_right_strip);
    }
  }
};

} // namespace detail
} // namespace regex
} // namespace folly
