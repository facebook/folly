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

#include <folly/regex/Flags.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/Executor.h>

namespace folly::regex::detail {

// Compile-time trait: can the node at `Idx` in `Ast` be tested with a single
// character check? When true, matchRepeat uses a tight iterative loop instead
// of recursive CPS — the key optimization for quantified character classes.
//
// For CharClass nodes with 1-2 ranges, uses direct inline comparisons.
// For CharClass nodes with ≥3 ranges, uses the compact bitmap from the AST
// (O(1) word lookup). Falls back to charClassTestAt for edge cases.
template <const auto& Ast, int Idx>
struct CharTestTrait {
  static constexpr bool available = [] {
    if constexpr (Idx < 0) {
      return false;
    } else {
      constexpr auto kind = Ast.nodes[Idx].kind;
      if constexpr (kind == NodeKind::Literal) {
        return Ast.nodes[Idx].literal.size() == 1;
      }
      return kind == NodeKind::AnyByte || kind == NodeKind::CharClass;
    }
  }();

 private:
  static constexpr int kCCIdx = [] {
    if constexpr (Idx >= 0 && Ast.nodes[Idx].kind == NodeKind::CharClass) {
      return Ast.nodes[Idx].char_class_index;
    } else {
      return -1;
    }
  }();

 public:
  static bool testChar(char c) noexcept {
    if constexpr (Idx < 0) {
      return false;
    } else {
      constexpr auto& n = Ast.nodes[Idx];
      if constexpr (n.kind == NodeKind::Literal) {
        return c == n.literal[0];
      } else if constexpr (n.kind == NodeKind::AnyByte) {
        return true;
      } else if constexpr (n.kind == NodeKind::CharClass) {
        constexpr auto& cc = Ast.char_classes[kCCIdx];
        if constexpr (cc.range_count == 1) {
          constexpr auto r = Ast.ranges[cc.range_offset];
          auto uc = static_cast<unsigned char>(c);
          return (uc >= r.lo && uc <= r.hi);
        } else if constexpr (cc.range_count == 2) {
          constexpr auto r0 = Ast.ranges[cc.range_offset];
          constexpr auto r1 = Ast.ranges[cc.range_offset + 1];
          auto uc = static_cast<unsigned char>(c);
          return (uc >= r0.lo && uc <= r0.hi) || (uc >= r1.lo && uc <= r1.hi);
        } else if constexpr (cc.range_count == 3) {
          constexpr auto r0 = Ast.ranges[cc.range_offset];
          constexpr auto r1 = Ast.ranges[cc.range_offset + 1];
          constexpr auto r2 = Ast.ranges[cc.range_offset + 2];
          auto uc = static_cast<unsigned char>(c);
          return (uc >= r0.lo && uc <= r0.hi) || (uc >= r1.lo && uc <= r1.hi) ||
              (uc >= r2.lo && uc <= r2.hi);
        } else if constexpr (cc.range_count == 4) {
          constexpr auto r0 = Ast.ranges[cc.range_offset];
          constexpr auto r1 = Ast.ranges[cc.range_offset + 1];
          constexpr auto r2 = Ast.ranges[cc.range_offset + 2];
          constexpr auto r3 = Ast.ranges[cc.range_offset + 3];
          auto uc = static_cast<unsigned char>(c);
          return (uc >= r0.lo && uc <= r0.hi) || (uc >= r1.lo && uc <= r1.hi) ||
              (uc >= r2.lo && uc <= r2.hi) || (uc >= r3.lo && uc <= r3.hi);
        } else {
          return Ast.charClassTestAt(kCCIdx, c);
        }
      } else {
        return false;
      }
    }
  }
};

// Find the first consuming (non-zero-width) node index from the root.
// Skips Sequence containers, non-capturing Groups, and Empty nodes.
// Used to detect if a Repeat is the first consuming element, enabling
// the sliding window optimization in matchRepeat.
constexpr int findFirstConsumingNode(const auto& ast, int nodeIdx) noexcept {
  if (nodeIdx < 0) {
    return -1;
  }
  const auto& n = ast.nodes[nodeIdx];
  if (n.kind == NodeKind::Sequence) {
    int child = n.child_first;
    while (child >= 0) {
      int result = findFirstConsumingNode(ast, child);
      if (result >= 0) {
        return result;
      }
      child = ast.nodes[child].next_sibling;
    }
    return -1;
  }
  if (n.kind == NodeKind::Group && !n.capturing) {
    return findFirstConsumingNode(ast, n.child_first);
  }
  if (n.kind == NodeKind::Empty) {
    return -1;
  }
  return nodeIdx;
}

// Compile-time collection of capturing group IDs reachable from an AST
// subtree. Used by matchRepeatPossessive to save/restore only the groups
// that the inner expression can modify, instead of copying full state.
template <int NumGroups>
struct ReachableGroups {
  int ids[NumGroups > 0 ? NumGroups : 1] = {};
  int count = 0;
};

template <int NumGroups>
constexpr ReachableGroups<NumGroups> collectReachableGroups(
    const auto& ast,
    int nodeIdx,
    ReachableGroups<NumGroups> result = {}) noexcept {
  if (nodeIdx < 0) {
    return result;
  }
  const auto& n = ast.nodes[nodeIdx];
  if (n.kind == NodeKind::Group && n.capturing) {
    result.ids[result.count++] = n.group_id;
  }
  switch (n.kind) {
    case NodeKind::Sequence:
    case NodeKind::Alternation: {
      int child = n.child_first;
      while (child >= 0) {
        result = collectReachableGroups(ast, child, result);
        child = ast.nodes[child].next_sibling;
      }
      break;
    }
    case NodeKind::Group:
    case NodeKind::Repeat:
      result = collectReachableGroups(ast, n.child_first, result);
      break;
    // Lookaround runs with its own MatchState<0> and TrackCaptures=false,
    // so groups inside lookaround don't modify the outer capture state.
    case NodeKind::Lookahead:
    case NodeKind::NegLookahead:
    case NodeKind::Lookbehind:
    case NodeKind::NegLookbehind:
    case NodeKind::Empty:
    case NodeKind::Literal:
    case NodeKind::AnyByte:
    case NodeKind::CharClass:
    case NodeKind::Anchor:
    case NodeKind::WordBoundary:
    case NodeKind::NegWordBoundary:
    case NodeKind::Backref:
    case NodeKind::CaseInsensitiveBackref:
    case NodeKind::Dead:
      break;
  }
  return result;
}

// Compile-time trait: can the inner expression of a Repeat be matched as a
// fixed-width compound test? Applies when the inner is non-backtracking with
// a positive fixed width > 1 (single-char cases are already handled by
// CharTestTrait). When available, matchRepeat uses a tight iterative loop
// that matches the full inner expression at each iteration.
template <const auto& Ast, int Idx>
struct CompoundRepeatTrait {
  static constexpr bool available = [] {
    if constexpr (Idx < 0) {
      return false;
    } else if constexpr (CharTestTrait<Ast, Idx>::available) {
      return false; // Already handled by CharTestTrait
    } else {
      return isNonBacktracking(Ast, Idx) && computeFixedWidth(Ast, Idx) > 0;
    }
  }();

  static constexpr int width = [] {
    if constexpr (Idx < 0) {
      return 0;
    } else {
      return computeFixedWidth(Ast, Idx);
    }
  }();
};

template <
    const auto& Ast,
    const auto& ForwardAst,
    Flags F,
    int NodeIdx,
    bool TrackCaptures,
    int NumGroups,
    bool AllowSlide = false,
    Direction Dir = Direction::Forward>
struct BacktrackExecutor {
  static constexpr auto& node = Ast.nodes[NodeIdx];

  template <typename Cont>
  static bool match(
      InputView<Dir> input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if constexpr (node.kind == NodeKind::Empty) {
      return cont(pos);
    } else if constexpr (node.kind == NodeKind::Literal) {
      constexpr auto lit = node.literal;
      if constexpr (Dir == Direction::Forward) {
        if (pos + lit.size() <= input.size()) {
          bool eq = true;
          for (std::size_t i = 0; i < lit.size() && eq; ++i) {
            eq = (input[pos + i] == lit[i]);
          }
          if (eq) {
            return cont(pos + lit.size());
          }
        }
      } else {
        if (pos >= lit.size()) {
          bool match = true;
          for (std::size_t i = 0; i < lit.size() && match; ++i) {
            match = (input[pos - 1 - i] == lit[i]);
          }
          if (match) {
            return cont(pos - lit.size());
          }
        }
      }
      return false;
    } else if constexpr (node.kind == NodeKind::AnyByte) {
      if (input.canConsume(pos)) {
        return cont(InputView<Dir>::advance(pos));
      }
      return false;
    } else if constexpr (node.kind == NodeKind::CharClass) {
      if (input.canConsume(pos) &&
          Ast.charClassTestAt(node.char_class_index, input.charAt(pos))) {
        return cont(InputView<Dir>::advance(pos));
      }
      return false;
    } else if constexpr (node.kind == NodeKind::Anchor) {
      if constexpr (node.anchor == AnchorKind::Begin) {
        if (pos == 0) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::End) {
        if (pos == input.size()) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::StartOfString) {
        if (pos == 0) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::EndOfString) {
        if (pos == input.size()) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::EndOfStringOrNewline) {
        if (pos == input.size() ||
            (pos + 1 == input.size() && input[pos] == '\n')) {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::BeginLine) {
        if (pos == 0 || input[pos - 1] == '\n') {
          return cont(pos);
        }
        return false;
      } else if constexpr (node.anchor == AnchorKind::EndLine) {
        if (pos == input.size() || input[pos] == '\n') {
          return cont(pos);
        }
        return false;
      } else {
        return false;
      }
    } else if constexpr (node.kind == NodeKind::WordBoundary) {
      bool prevWord = pos > 0 && isWordChar(input[pos - 1]);
      bool currWord = pos < input.size() && isWordChar(input[pos]);
      if (prevWord != currWord) {
        return cont(pos);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::NegWordBoundary) {
      bool prevWord = pos > 0 && isWordChar(input[pos - 1]);
      bool currWord = pos < input.size() && isWordChar(input[pos]);
      if (prevWord == currWord) {
        return cont(pos);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::Group) {
      if constexpr (TrackCaptures && node.capturing) {
        auto saved = state.groups[node.group_id];
        state.groups[node.group_id].offset = pos;
        bool result = BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            node.child_first,
            TrackCaptures,
            NumGroups,
            AllowSlide,
            Dir>::match(input, pos, state, budget, [&](std::size_t endPos) {
          if constexpr (Dir == Direction::Reverse) {
            auto absStart = input.posInFull(endPos);
            auto absEnd = input.posInFull(pos);
            state.groups[node.group_id].offset = absStart;
            state.groups[node.group_id].length = absEnd - absStart;
          } else {
            state.groups[node.group_id].length = endPos - pos;
          }
          return cont(endPos);
        });
        if (!result) {
          state.groups[node.group_id] = saved;
        }
        return result;
      } else {
        return BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            node.child_first,
            TrackCaptures,
            NumGroups,
            AllowSlide,
            Dir>::match(input, pos, state, budget, cont);
      }
    } else if constexpr (node.kind == NodeKind::Sequence) {
      return matchSequence<node.child_first>(input, pos, state, budget, cont);
    } else if constexpr (node.kind == NodeKind::Alternation) {
      constexpr auto altFilter = extractFirstCharFilter(Ast, NodeIdx);
      if constexpr (!altFilter.accepts_all && Dir == Direction::Forward) {
        if (pos >= input.size() || !altFilter.test(input[pos])) {
          return false;
        }
      }
      if constexpr (
          node.discriminator_offset >= 0 && Dir == Direction::Forward) {
        if (pos + node.discriminator_offset >= input.size()) {
          return false;
        }
        return matchAlternationDispatched<
            node.child_first,
            node.discriminator_offset>(input, pos, state, budget, cont);
      }
      return matchAlternation<node.child_first>(
          input, pos, state, budget, cont);
    } else if constexpr (node.kind == NodeKind::Repeat) {
      return matchRepeat(input, pos, state, budget, cont);
    } else if constexpr (node.kind == NodeKind::Lookahead) {
      // Positive lookahead: match inner without consuming input.
      // Lookahead ALWAYS runs forward, regardless of main match direction.
      // Inner runs with TrackCaptures=false and NumGroups=0, so it won't
      // modify any capture state — no need to save/restore the outer state.
      MatchState<0> innerState;
      bool innerMatched;
      if constexpr (Dir == Direction::Forward) {
        innerMatched = BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            node.child_first,
            false,
            0,
            false,
            Direction::Forward>::
            match(input, pos, innerState, budget, [](std::size_t) {
              return true;
            });
      } else {
        // Reverse mode: run the forward inner from ForwardAst.
        // Use probe_id to find the inner in the ProbeStore.
        constexpr int fwdInner = ForwardAst.hasProbe(node.probe_id)
            ? ForwardAst.probes[node.probe_id].root
            : -1;
        auto fwdInput = input.template fullInput<Direction::Forward>();
        innerMatched = BacktrackExecutor<
            ForwardAst,
            ForwardAst,
            F,
            fwdInner,
            false,
            0,
            false,
            Direction::Forward>::
            match(
                fwdInput,
                input.posInFull(pos),
                innerState,
                budget,
                [](std::size_t) { return true; });
      }
      if (innerMatched) {
        return cont(pos);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::NegLookahead) {
      // Negative lookahead: fail if inner matches.
      // Lookahead ALWAYS runs forward.
      // Inner runs with TrackCaptures=false and NumGroups=0, so it won't
      // modify any capture state — no need to save/restore the outer state.
      MatchState<0> innerState;
      bool innerMatched;
      if constexpr (Dir == Direction::Forward) {
        innerMatched = BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            node.child_first,
            false,
            0,
            false,
            Direction::Forward>::
            match(input, pos, innerState, budget, [](std::size_t) {
              return true;
            });
      } else {
        constexpr int fwdInner = ForwardAst.hasProbe(node.probe_id)
            ? ForwardAst.probes[node.probe_id].root
            : -1;
        auto fwdInput = input.template fullInput<Direction::Forward>();
        innerMatched = BacktrackExecutor<
            ForwardAst,
            ForwardAst,
            F,
            fwdInner,
            false,
            0,
            false,
            Direction::Forward>::
            match(
                fwdInput,
                input.posInFull(pos),
                innerState,
                budget,
                [](std::size_t) { return true; });
      }
      if (!innerMatched) {
        return cont(pos);
      }
      return false;
    } else if constexpr (node.kind == NodeKind::Lookbehind) {
      // Positive lookbehind: match inner ending at current position.
      // Variable-width lookbehinds run the inner pattern in reverse
      // from pos, checking if any match ends at pos going leftward.
      constexpr int width = node.min_repeat;
      if constexpr (width < 0) {
        // Inner runs with TrackCaptures=false and NumGroups=0, so it won't
        // modify any capture state — no need to save/restore the outer state.
        MatchState<0> innerState;
        bool innerMatched;
        if constexpr (Dir == Direction::Forward) {
          // Forward execution: use the reverse probe from the probe
          // store (which has the reversed inner pattern) and run it
          // in reverse direction from pos.
          constexpr int revInner = ForwardAst.hasProbe(node.probe_id)
              ? ForwardAst.probes[node.probe_id].root
              : -1;
          if constexpr (revInner >= 0) {
            auto revInput = input.template fullInput<Direction::Reverse>();
            std::size_t fwdPos = input.posInFull(pos);
            innerMatched = BacktrackExecutor<
                ForwardAst,
                ForwardAst,
                F,
                revInner,
                false,
                0,
                false,
                Direction::Reverse>::
                match(revInput, fwdPos, innerState, budget, [](std::size_t) {
                  return true;
                });
          } else {
            // Fallback: iterate start positions forward
            innerMatched = false;
            for (std::size_t start = 0; start <= pos && !innerMatched;
                 ++start) {
              innerMatched = BacktrackExecutor<
                  Ast,
                  ForwardAst,
                  F,
                  node.child_first,
                  false,
                  0,
                  false,
                  Direction::Forward>::
                  match(
                      input,
                      start,
                      innerState,
                      budget,
                      [pos](std::size_t endPos) { return endPos == pos; });
            }
          }
        } else {
          // Reverse execution: the main AST (Ast) is reversed, so
          // the lookbehind's child_first contains the reversed inner
          // pattern. Run it in reverse direction from fwdPos — this
          // scans leftward, naturally checking if any match of the
          // inner pattern ends at the current position.
          std::size_t fwdPos = input.posInFull(pos);
          auto revInput = input.template fullInput<Direction::Reverse>();
          innerMatched = BacktrackExecutor<
              Ast,
              ForwardAst,
              F,
              node.child_first,
              false,
              0,
              false,
              Direction::Reverse>::
              match(revInput, fwdPos, innerState, budget, [](std::size_t) {
                return true;
              });
        }
        if (innerMatched) {
          return cont(pos);
        }
        return false;
      } else {
        std::size_t fullPos;
        if constexpr (Dir == Direction::Forward) {
          fullPos = input.posInFull(pos);
        } else {
          fullPos = input.posInFull(pos);
        }
        if (fullPos < static_cast<std::size_t>(width)) {
          return false;
        }
        // Inner runs with TrackCaptures=false and NumGroups=0, so it won't
        // modify any capture state — no need to save/restore the outer state.
        MatchState<0> innerState;
        bool innerMatched;
        if constexpr (Dir == Direction::Forward) {
          innerMatched = BacktrackExecutor<
              Ast,
              ForwardAst,
              F,
              node.child_first,
              false,
              0,
              false,
              Direction::Forward>::
              match(
                  input,
                  fullPos - width,
                  innerState,
                  budget,
                  [fullPos](std::size_t endPos) { return endPos == fullPos; });
        } else {
          // Reverse mode: use the forward AST's probe inner and run forward.
          constexpr int fwdInner = ForwardAst.hasProbe(node.probe_id)
              ? ForwardAst.probes[node.probe_id].root
              : -1;
          auto fwdInput = input.template fullInput<Direction::Forward>();
          innerMatched = BacktrackExecutor<
              ForwardAst,
              ForwardAst,
              F,
              fwdInner,
              false,
              0,
              false,
              Direction::Forward>::
              match(
                  fwdInput,
                  fullPos - width,
                  innerState,
                  budget,
                  [fullPos](std::size_t endPos) { return endPos == fullPos; });
        }
        if (innerMatched) {
          return cont(pos);
        }
        return false;
      }
    } else if constexpr (node.kind == NodeKind::NegLookbehind) {
      // Negative lookbehind: fail if inner matches ending at current position.
      // Variable-width lookbehinds run the inner pattern in reverse from
      // pos. If any match is found, the negative lookbehind fails.
      constexpr int width = node.min_repeat;
      if constexpr (width < 0) {
        // Inner runs with TrackCaptures=false and NumGroups=0, so it won't
        // modify any capture state — no need to save/restore the outer state.
        MatchState<0> innerState;
        bool anyMatched = false;
        if constexpr (Dir == Direction::Forward) {
          // Forward execution: use the reverse probe from the probe
          // store (which has the reversed inner pattern) and run it
          // in reverse direction from pos.
          constexpr int revInner = ForwardAst.hasProbe(node.probe_id)
              ? ForwardAst.probes[node.probe_id].root
              : -1;
          if constexpr (revInner >= 0) {
            auto revInput = input.template fullInput<Direction::Reverse>();
            std::size_t fwdPos = input.posInFull(pos);
            anyMatched = BacktrackExecutor<
                ForwardAst,
                ForwardAst,
                F,
                revInner,
                false,
                0,
                false,
                Direction::Reverse>::
                match(revInput, fwdPos, innerState, budget, [](std::size_t) {
                  return true;
                });
          } else {
            // Fallback: iterate start positions forward
            for (std::size_t start = 0; start <= pos && !anyMatched; ++start) {
              bool innerMatched = BacktrackExecutor<
                  Ast,
                  ForwardAst,
                  F,
                  node.child_first,
                  false,
                  0,
                  false,
                  Direction::Forward>::
                  match(
                      input,
                      start,
                      innerState,
                      budget,
                      [pos](std::size_t endPos) { return endPos == pos; });
              if (innerMatched) {
                anyMatched = true;
              }
            }
          }
        } else {
          // Reverse execution: run the reversed inner pattern in
          // reverse direction from fwdPos. If it matches (reaching
          // any position going left), the negative lookbehind fails.
          auto revInput = input.template fullInput<Direction::Reverse>();
          std::size_t fwdPos = input.posInFull(pos);
          anyMatched = BacktrackExecutor<
              Ast,
              ForwardAst,
              F,
              node.child_first,
              false,
              0,
              false,
              Direction::Reverse>::
              match(revInput, fwdPos, innerState, budget, [](std::size_t) {
                return true;
              });
        }
        if (anyMatched) {
          // Inner matched — negative lookbehind fails
          return false;
        }
        // No match — negative lookbehind passes
        return cont(pos);
      } else {
        std::size_t fullPos;
        if constexpr (Dir == Direction::Forward) {
          fullPos = input.posInFull(pos);
        } else {
          fullPos = input.posInFull(pos);
        }
        if (fullPos < static_cast<std::size_t>(width)) {
          return cont(pos);
        }
        // Inner runs with TrackCaptures=false and NumGroups=0, so it won't
        // modify any capture state — no need to save/restore the outer state.
        MatchState<0> innerState;
        bool innerMatched;
        if constexpr (Dir == Direction::Forward) {
          innerMatched = BacktrackExecutor<
              Ast,
              ForwardAst,
              F,
              node.child_first,
              false,
              0,
              false,
              Direction::Forward>::
              match(
                  input,
                  fullPos - width,
                  innerState,
                  budget,
                  [fullPos](std::size_t endPos) { return endPos == fullPos; });
        } else {
          constexpr int fwdInner = ForwardAst.hasProbe(node.probe_id)
              ? ForwardAst.probes[node.probe_id].root
              : -1;
          auto fwdInput = input.template fullInput<Direction::Forward>();
          innerMatched = BacktrackExecutor<
              ForwardAst,
              ForwardAst,
              F,
              fwdInner,
              false,
              0,
              false,
              Direction::Forward>::
              match(
                  fwdInput,
                  fullPos - width,
                  innerState,
                  budget,
                  [fullPos](std::size_t endPos) { return endPos == fullPos; });
        }
        if (!innerMatched) {
          return cont(pos);
        }
        return false;
      }
    } else if constexpr (
        node.kind == NodeKind::Backref ||
        node.kind == NodeKind::CaseInsensitiveBackref) {
      // Backreference: match same text as captured group.
      // For CaseInsensitiveBackref, comparison uses ASCII case folding.
      if constexpr (TrackCaptures) {
        constexpr bool kCaseInsensitive =
            node.kind == NodeKind::CaseInsensitiveBackref;
        const auto& captured = state.groups[node.group_id];
        if (captured.offset == std::string_view::npos) {
          return false;
        }
        std::size_t len = captured.length;
        if constexpr (Dir == Direction::Forward) {
          if (pos + len > input.size()) {
            return false;
          }
          for (std::size_t i = 0; i < len; ++i) {
            char a = input[pos + i];
            char b = input[captured.offset + i];
            if constexpr (kCaseInsensitive) {
              if (asciiToLower(a) != asciiToLower(b)) {
                return false;
              }
            } else {
              if (a != b) {
                return false;
              }
            }
          }
          return cont(pos + len);
        } else {
          if (pos < len) {
            return false;
          }
          for (std::size_t i = 0; i < len; ++i) {
            char a = input[pos - len + i];
            char b = input[captured.offset + i];
            if constexpr (kCaseInsensitive) {
              if (asciiToLower(a) != asciiToLower(b)) {
                return false;
              }
            } else {
              if (a != b) {
                return false;
              }
            }
          }
          return cont(pos - len);
        }
      } else {
        return false;
      }
    } else {
      return false;
    }
  }

  template <int ChildIdx, typename Cont>
  static bool matchSequence(
      InputView<Dir> input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if constexpr (ChildIdx < 0) {
      return cont(pos);
    } else if constexpr (isNonBacktracking(Ast, ChildIdx)) {
      // Flat match: this child has a single deterministic outcome.
      // Match it directly and advance without nesting a continuation.
      bool matched = BacktrackExecutor<
          Ast,
          ForwardAst,
          F,
          ChildIdx,
          TrackCaptures,
          NumGroups,
          AllowSlide,
          Dir>::match(input, pos, state, budget, [&](std::size_t nextPos) {
        pos = nextPos;
        return true;
      });
      if (!matched) {
        return false;
      }
      constexpr int nextChild = Ast.nodes[ChildIdx].next_sibling;
      return matchSequence<nextChild>(input, pos, state, budget, cont);
    } else {
      // Backtracking child: use continuation-nesting so the child
      // can retry with different match lengths on failure.
      constexpr int nextChild = Ast.nodes[ChildIdx].next_sibling;
      if constexpr (nextChild < 0) {
        // Last child in sequence — pass continuation directly (tail call),
        // eliminating one lambda frame from the CPS chain.
        return BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            ChildIdx,
            TrackCaptures,
            NumGroups,
            AllowSlide,
            Dir>::match(input, pos, state, budget, cont);
      } else {
        return BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            ChildIdx,
            TrackCaptures,
            NumGroups,
            AllowSlide,
            Dir>::match(input, pos, state, budget, [&](std::size_t nextPos) {
          return matchSequence<nextChild>(input, nextPos, state, budget, cont);
        });
      }
    }
  }

  template <int AltIdx, typename Cont>
  static bool matchAlternation(
      InputView<Dir> input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if constexpr (AltIdx < 0) {
      return false;
    } else {
      if (budget == 0) {
        return false;
      }
      --budget;
      constexpr int nextAlt = Ast.nodes[AltIdx].next_sibling;
      if (BacktrackExecutor<
              Ast,
              ForwardAst,
              F,
              AltIdx,
              TrackCaptures,
              NumGroups,
              AllowSlide,
              Dir>::match(input, pos, state, budget, cont)) {
        return true;
      }
      return matchAlternation<nextAlt>(input, pos, state, budget, cont);
    }
  }

  template <int AltIdx, int DiscOffset, typename Cont>
  static bool matchAlternationDispatched(
      InputView<Dir> input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    if constexpr (AltIdx < 0) {
      return false;
    } else {
      // Dispatched alternation selects branches via a discriminator
      // character — branches whose discriminator doesn't match are
      // skipped entirely, so iterating siblings here is not
      // backtracking and must not consume budget. Budget is reserved
      // for actual backtracking inside the chosen branch.
      constexpr int nextAlt = Ast.nodes[AltIdx].next_sibling;
      constexpr auto charInfo = resolveCharAtOffset(Ast, AltIdx, DiscOffset);
      bool tryBranch = false;
      if constexpr (charInfo.valid) {
        constexpr auto& dn = Ast.nodes[charInfo.nodeIdx];
        if constexpr (dn.kind == NodeKind::Literal) {
          tryBranch =
              (static_cast<unsigned char>(input[pos + DiscOffset]) ==
               static_cast<unsigned char>(dn.literal[charInfo.charOffset]));
        } else if constexpr (
            dn.kind == NodeKind::CharClass ||
            (dn.kind == NodeKind::Alternation && dn.char_class_index >= 0)) {
          tryBranch =
              Ast.charClassTestAt(dn.char_class_index, input[pos + DiscOffset]);
        }
      } else {
        tryBranch = true;
      }
      if (tryBranch) {
        if (BacktrackExecutor<
                Ast,
                ForwardAst,
                F,
                AltIdx,
                TrackCaptures,
                NumGroups,
                AllowSlide,
                Dir>::match(input, pos, state, budget, cont)) {
          return true;
        }
      }
      return matchAlternationDispatched<nextAlt, DiscOffset>(
          input, pos, state, budget, cont);
    }
  }

  template <typename Cont>
  static bool matchRepeat(
      InputView<Dir> input,
      std::size_t pos,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    constexpr int innerIdx = node.child_first;
    constexpr int minR = node.min_repeat;
    constexpr int maxR = node.max_repeat;

    // Optimization: when the inner expression is a simple character test,
    // use a tight iterative scan loop instead of recursive CPS.
    if constexpr (CharTestTrait<Ast, innerIdx>::available) {
      if constexpr (node.isPossessive()) {
        // Possessive: consume maximum, never backtrack
        std::size_t start = pos;
        std::size_t count = 0;
        std::size_t limit = maxR >= 0
            ? static_cast<std::size_t>(maxR)
            : (Dir == Direction::Forward ? input.size() - pos : pos);
        while (count < limit && input.canConsume(pos) &&
               CharTestTrait<Ast, innerIdx>::testChar(input.charAt(pos))) {
          pos = InputView<Dir>::advance(pos);
          ++count;
        }
        if constexpr (Dir == Direction::Reverse) {
          // Check if the possessive could consume one more iteration
          // forward from where it started. If so, the possessive would
          // have consumed that content in forward mode — fail.
          // Skip this probe for optimizer-inferred possessives: the
          // optimizer verified the follow set is disjoint, so the probe
          // is unnecessary and would false-positive when preceding
          // sequence siblings already consumed input[start].
          if constexpr (node.isPossessiveProbed()) {
            if ((maxR < 0 || count < static_cast<std::size_t>(maxR)) &&
                start < input.size() &&
                CharTestTrait<Ast, innerIdx>::testChar(input[start])) {
              return false;
            }
          }
        }
        if (static_cast<int>(count) >= minR) {
          if (cont(pos)) {
            return true;
          }
        }
        // Sliding window for possessive root repeat: no backtracking
        // within each position, just try cont at the possessive count.
        constexpr bool isRootRepeat = AllowSlide && Dir == Direction::Forward &&
            (findFirstConsumingNode(Ast, Ast.root) == NodeIdx);
        if constexpr (isRootRepeat) {
          std::size_t totalScanned = count;
          std::size_t scanEnd = pos;
          if (totalScanned > static_cast<std::size_t>(minR) && minR > 0) {
            if (totalScanned == limit && maxR >= 0) {
              while (scanEnd < input.size() &&
                     CharTestTrait<Ast, innerIdx>::testChar(input[scanEnd])) {
                ++scanEnd;
              }
            }
            for (std::size_t offset = 1;
                 start + offset + static_cast<std::size_t>(minR) <= scanEnd;
                 ++offset) {
              std::size_t newStart = start + offset;
              std::size_t available = scanEnd - newStart;
              std::size_t effectiveMax = maxR >= 0
                  ? std::min(available, static_cast<std::size_t>(maxR))
                  : available;
              if (static_cast<int>(effectiveMax) >= minR) {
                if (cont(newStart + effectiveMax)) {
                  if constexpr (TrackCaptures) {
                    state.groups[0].offset = newStart;
                  }
                  return true;
                }
              }
            }
          }
        }
        return false;
      } else if constexpr (node.repeat_mode == RepeatMode::Greedy) {
        // Greedy: scan forward as far as possible, then backtrack
        std::size_t start = pos;
        std::size_t count = 0;
        std::size_t limit = maxR >= 0
            ? static_cast<std::size_t>(maxR)
            : (Dir == Direction::Forward ? input.size() - pos : pos);
        while (count < limit && input.canConsume(pos) &&
               CharTestTrait<Ast, innerIdx>::testChar(input.charAt(pos))) {
          pos = InputView<Dir>::advance(pos);
          ++count;
        }
        std::size_t totalScanned = count;
        std::size_t scanEnd = pos;
        // Backtrack from longest to shortest, trying continuation
        while (true) {
          if (static_cast<int>(count) >= minR) {
            if (cont(pos)) {
              return true;
            }
          }
          if constexpr (Dir == Direction::Forward) {
            if (pos <= start) {
              break;
            }
            --pos;
          } else {
            if (pos >= start) {
              break;
            }
            ++pos;
          }
          --count;
        }
        // Sliding window: when this repeat is the first consuming
        // element in the pattern, slide the start forward within the
        // already-scanned region instead of returning to the search loop.
        constexpr bool isRootRepeat = AllowSlide && Dir == Direction::Forward &&
            (findFirstConsumingNode(Ast, Ast.root) == NodeIdx);
        if constexpr (isRootRepeat) {
          if (totalScanned > static_cast<std::size_t>(minR) && minR > 0) {
            // If the original scan was limit-capped (hit maxR), extend
            // to find the full region of verified characters.
            if (totalScanned == limit && maxR >= 0) {
              while (scanEnd < input.size() &&
                     CharTestTrait<Ast, innerIdx>::testChar(input[scanEnd])) {
                ++scanEnd;
              }
            }
            for (std::size_t offset = 1;
                 start + offset + static_cast<std::size_t>(minR) <= scanEnd;
                 ++offset) {
              std::size_t newStart = start + offset;
              std::size_t available = scanEnd - newStart;
              std::size_t effectiveMax = maxR >= 0
                  ? std::min(available, static_cast<std::size_t>(maxR))
                  : available;
              pos = newStart + effectiveMax;
              count = effectiveMax;
              while (true) {
                if (static_cast<int>(count) >= minR) {
                  if (cont(pos)) {
                    if constexpr (TrackCaptures) {
                      state.groups[0].offset = newStart;
                    }
                    return true;
                  }
                }
                if (count <= static_cast<std::size_t>(minR)) {
                  break;
                }
                --pos;
                --count;
              }
            }
          }
        }
        return false;
      } else {
        // Lazy: try continuation first with minimum matches, then extend
        std::size_t count = 0;
        // First match minimum required
        for (int i = 0; i < minR; ++i) {
          if (!input.canConsume(pos) ||
              !CharTestTrait<Ast, innerIdx>::testChar(input.charAt(pos))) {
            return false;
          }
          pos = InputView<Dir>::advance(pos);
          ++count;
        }
        // Try continuation at each position from min to max
        while (true) {
          if (cont(pos)) {
            return true;
          }
          if (maxR >= 0 && count >= static_cast<std::size_t>(maxR)) {
            break;
          }
          if (!input.canConsume(pos) ||
              !CharTestTrait<Ast, innerIdx>::testChar(input.charAt(pos))) {
            break;
          }
          pos = InputView<Dir>::advance(pos);
          ++count;
        }
        return false;
      }
    } else if constexpr (
        CompoundRepeatTrait<Ast, innerIdx>::available && !node.isPossessive()) {
      // Fixed-width non-backtracking inner (multi-char): use an iterative
      // loop that matches the full inner at each iteration, following the
      // same pattern as CharTestTrait but consuming multiple chars per step.
      // Possessive repeats are excluded — they need the full
      // matchRepeatPossessive path with reverse probe verification.
      auto tryInner = [&](std::size_t p) -> std::size_t {
        std::size_t result = p;
        BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            innerIdx,
            TrackCaptures,
            NumGroups,
            AllowSlide,
            Dir>::match(input, p, state, budget, [&](std::size_t endPos) {
          result = endPos;
          return true;
        });
        return result;
      };
      if constexpr (node.repeat_mode == RepeatMode::Greedy) {
        constexpr int kFallbackMax = 256;
        constexpr int kMaxPositions =
            (maxR >= 0 && maxR < kFallbackMax) ? maxR : kFallbackMax;
        std::size_t positions[kMaxPositions + 1];
        int posCount = 0;
        positions[0] = pos;
        constexpr std::size_t limit = static_cast<std::size_t>(kMaxPositions);
        while (static_cast<std::size_t>(posCount) < limit) {
          auto nextPos = tryInner(pos);
          if (nextPos == pos) {
            break;
          }
          pos = nextPos;
          positions[++posCount] = pos;
        }
        for (int i = posCount; i >= minR; --i) {
          if (cont(positions[i])) {
            return true;
          }
        }
        if (posCount == static_cast<int>(limit) &&
            (maxR < 0 || maxR > kFallbackMax)) {
          return matchRepeatGreedy<innerIdx, minR, maxR>(
              input, positions[posCount], posCount, state, budget, cont);
        }
        return false;
      } else {
        std::size_t count = 0;
        for (int i = 0; i < minR; ++i) {
          auto nextPos = tryInner(pos);
          if (nextPos == pos) {
            return false;
          }
          pos = nextPos;
          ++count;
        }
        while (true) {
          if (cont(pos)) {
            return true;
          }
          if (maxR >= 0 && count >= static_cast<std::size_t>(maxR)) {
            break;
          }
          auto nextPos = tryInner(pos);
          if (nextPos == pos) {
            break;
          }
          pos = nextPos;
          ++count;
        }
        return false;
      }
    } else {
      if constexpr (node.isPossessive()) {
        return matchRepeatPossessive<innerIdx, minR, maxR>(
            input, pos, 0, state, budget, cont);
      } else if constexpr (node.repeat_mode == RepeatMode::Greedy) {
        return matchRepeatGreedy<innerIdx, minR, maxR>(
            input, pos, 0, state, budget, cont);
      } else {
        return matchRepeatLazy<innerIdx, minR, maxR>(
            input, pos, 0, state, budget, cont);
      }
    }
  }

  template <int InnerIdx, int MinR, int MaxR, typename Cont>
  static bool matchRepeatGreedy(
      InputView<Dir> input,
      std::size_t pos,
      int count,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    // When the inner is non-backtracking, use an iterative scan-then-
    // backtrack loop instead of recursive CPS.
    if constexpr (isNonBacktracking(Ast, InnerIdx)) {
      constexpr int kFallbackMax = 256;
      constexpr int kMaxPositions =
          (MaxR >= 0 && MaxR < kFallbackMax) ? MaxR : kFallbackMax;
      std::size_t positions[kMaxPositions + 1];
      positions[0] = pos;
      int posCount = 0;

      std::size_t limit = MaxR >= 0
          ? static_cast<std::size_t>(MaxR) - count
          : static_cast<std::size_t>(kMaxPositions);
      if (limit > kMaxPositions) {
        limit = kMaxPositions;
      }

      while (static_cast<std::size_t>(posCount) < limit) {
        if (budget == 0) {
          return false;
        }
        --budget;
        std::size_t nextPos = pos;
        bool ok = BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            InnerIdx,
            TrackCaptures,
            NumGroups,
            AllowSlide,
            Dir>::match(input, pos, state, budget, [&](std::size_t p) {
          nextPos = p;
          return true;
        });
        if (!ok || nextPos == pos) {
          break;
        }
        pos = nextPos;
        positions[++posCount] = pos;
      }

      for (int i = posCount; i >= 0; --i) {
        if (count + i >= MinR) {
          if (cont(positions[i])) {
            return true;
          }
        }
      }
      // If we filled the positions buffer but haven't reached MaxR,
      // fall back to recursive CPS for the remaining iterations.
      if (posCount == static_cast<int>(limit) &&
          (MaxR < 0 || MaxR > kFallbackMax)) {
        return matchRepeatGreedy<InnerIdx, MinR, MaxR>(
            input, positions[posCount], count + posCount, state, budget, cont);
      }
      return false;
    } else {
      if (budget == 0) {
        return false;
      }
      --budget;

      if constexpr (MaxR >= 0) {
        if (count >= MaxR) {
          if (count >= MinR) {
            return cont(pos);
          }
          return false;
        }
      }

      if (BacktrackExecutor<
              Ast,
              ForwardAst,
              F,
              InnerIdx,
              TrackCaptures,
              NumGroups,
              AllowSlide,
              Dir>::match(input, pos, state, budget, [&](std::size_t nextPos) {
            if (nextPos == pos) {
              return false;
            }
            return matchRepeatGreedy<InnerIdx, MinR, MaxR>(
                input, nextPos, count + 1, state, budget, cont);
          })) {
        return true;
      }

      if (count >= MinR) {
        return cont(pos);
      }
      return false;
    }
  }

  template <int InnerIdx, int MinR, int MaxR, typename Cont>
  static bool matchRepeatLazy(
      InputView<Dir> input,
      std::size_t pos,
      int count,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont) noexcept {
    // When the inner is non-backtracking, use an iterative loop instead
    // of recursive CPS.
    if constexpr (isNonBacktracking(Ast, InnerIdx)) {
      for (int i = count; i < MinR; ++i) {
        if (budget == 0) {
          return false;
        }
        --budget;
        std::size_t nextPos = pos;
        bool ok = BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            InnerIdx,
            TrackCaptures,
            NumGroups,
            AllowSlide,
            Dir>::match(input, pos, state, budget, [&](std::size_t p) {
          nextPos = p;
          return true;
        });
        if (!ok || nextPos == pos) {
          return false;
        }
        pos = nextPos;
        ++count;
      }
      while (true) {
        if (cont(pos)) {
          return true;
        }
        if constexpr (MaxR >= 0) {
          if (count >= MaxR) {
            break;
          }
        }
        if (budget == 0) {
          return false;
        }
        --budget;
        std::size_t nextPos = pos;
        bool ok = BacktrackExecutor<
            Ast,
            ForwardAst,
            F,
            InnerIdx,
            TrackCaptures,
            NumGroups,
            AllowSlide,
            Dir>::match(input, pos, state, budget, [&](std::size_t p) {
          nextPos = p;
          return true;
        });
        if (!ok || nextPos == pos) {
          break;
        }
        pos = nextPos;
        ++count;
      }
      return false;
    } else {
      if (budget == 0) {
        return false;
      }
      --budget;

      if (count >= MinR) {
        if (cont(pos)) {
          return true;
        }
      }

      if constexpr (MaxR >= 0) {
        if (count >= MaxR) {
          return false;
        }
      }

      return BacktrackExecutor<
          Ast,
          ForwardAst,
          F,
          InnerIdx,
          TrackCaptures,
          NumGroups,
          AllowSlide,
          Dir>::match(input, pos, state, budget, [&](std::size_t nextPos) {
        if (nextPos == pos) {
          return false;
        }
        return matchRepeatLazy<InnerIdx, MinR, MaxR>(
            input, nextPos, count + 1, state, budget, cont);
      });
    }
  }

  template <int InnerIdx, int MinR, int MaxR, typename Cont>
  static bool matchRepeatPossessive(
      InputView<Dir> input,
      std::size_t pos,
      int count,
      MatchState<NumGroups>& state,
      std::size_t& budget,
      Cont&& cont,
      std::size_t probePos = std::string_view::npos) noexcept {
    if (probePos == std::string_view::npos) {
      probePos = pos;
    }

    // Possessive: consume maximum iterations, then try continuation once.
    // Never backtrack to try fewer iterations.
    //
    // Save/restore only the capturing groups reachable from the inner
    // expression. When the inner has no capturing groups (e.g. a++,
    // [a-z]++, (?:ab)++), this is a complete no-op — the compiler
    // eliminates both loops via if constexpr.
    constexpr auto innerGroups =
        collectReachableGroups<NumGroups>(Ast, InnerIdx);
    while (true) {
      if (budget == 0) {
        return false;
      }
      --budget;

      if constexpr (MaxR >= 0) {
        if (count >= MaxR) {
          break;
        }
      }

      // Save only the capturing groups that the inner expression can modify.
      // When the inner has no capturing groups (e.g. a++, [a-z]++), this
      // is a complete no-op — the compiler eliminates both loops.
      GroupSpan savedGroups[innerGroups.count > 0 ? innerGroups.count : 1];
      if constexpr (innerGroups.count > 0) {
        for (int i = 0; i < innerGroups.count; ++i) {
          savedGroups[i] = state.groups[innerGroups.ids[i]];
        }
      }
      std::size_t matchedPos = pos;
      bool innerMatched = BacktrackExecutor<
          Ast,
          ForwardAst,
          F,
          InnerIdx,
          TrackCaptures,
          NumGroups,
          AllowSlide,
          Dir>::match(input, pos, state, budget, [&](std::size_t nextPos) {
        matchedPos = nextPos;
        return true;
      });

      if (!innerMatched || matchedPos == pos) {
        // Inner didn't match or made no progress — stop iterating
        if constexpr (innerGroups.count > 0) {
          for (int i = 0; i < innerGroups.count; ++i) {
            state.groups[innerGroups.ids[i]] = savedGroups[i];
          }
        }
        break;
      }

      // Inner matched and advanced — commit to this iteration (possessive)
      pos = matchedPos;
      ++count;
    }

    // In reverse: check if the possessive could consume one more full
    // iteration forward from where it started. If so, the possessive
    // would have consumed that content in forward mode — fail.
    // Skip for optimizer-inferred possessives (see CharTestTrait probe).
    //
    // The AST has been reversed (reverseAst) for reverse execution, so
    // Sequence children are in reverse order. Probing the reversed inner
    // forward gives the wrong result for multi-character sequences.
    // Instead, probe the reversed inner in the REVERSE direction from
    // probePos + innerWidth: matching reversed children right-to-left
    // is equivalent to matching the original children left-to-right.
    if constexpr (Dir == Direction::Reverse && node.isPossessiveProbed()) {
      if constexpr (MinR == 1 && MaxR == 1) {
        // Atomic group (desugared to possessive {1,1}): verify that
        // the forward engine would commit to the same match width.
        // In reverse, alternation branches have reversed content which
        // can cause a different branch to win (e.g. (?>ab|a)b on "ab":
        // forward commits to "ab", reverse commits to "a"). Use
        // probe_id to find the corresponding inner node in the forward
        // AST and run it in the forward direction to compare.
        constexpr int fwdNodeIdx = ForwardAst.hasProbe(node.probe_id)
            ? ForwardAst.probes[node.probe_id].root
            : -1;
        static_assert(
            fwdNodeIdx >= 0, "Atomic group probe_id not found in ProbeStore");
        if (count == 1) {
          std::size_t reverseWidth = probePos - pos;
          MatchState<0> probeState;
          std::size_t fwdPos = input.posInFull(pos);
          std::size_t fwdEnd = fwdPos;
          auto fwdInput = input.template fullInput<Direction::Forward>();
          bool fwdMatched = BacktrackExecutor<
              ForwardAst,
              ForwardAst,
              F,
              fwdNodeIdx,
              false,
              0,
              false,
              Direction::Forward>::
              match(
                  fwdInput,
                  fwdPos,
                  probeState,
                  budget,
                  [&fwdEnd](std::size_t endPos) {
                    fwdEnd = endPos;
                    return true;
                  });
          if (!fwdMatched || (fwdEnd - fwdPos) > reverseWidth) {
            return false;
          }
        }
      } else if (MaxR < 0 || count < MaxR) {
        // Multi-iteration possessive: check if one more iteration
        // could have matched forward from where it started.
        // If MaxR was reached, the forward possessive wouldn't have
        // consumed more either, so the probe is unnecessary.
        constexpr std::size_t innerWidth =
            static_cast<std::size_t>(computeMinWidth(Ast, InnerIdx));
        if (innerWidth > 0 && probePos + innerWidth <= input.size()) {
          MatchState<0> probeState;
          std::size_t fwdProbeStart = input.posInFull(probePos);
          auto fwdInput = input.template fullInput<Direction::Forward>();
          bool canForward = BacktrackExecutor < ForwardAst, ForwardAst, F,
               ForwardAst.hasProbe(node.probe_id)
              ? ForwardAst.probes[node.probe_id].root
              : -1,
               false, 0, false,
               Direction::Forward >
              ::match(
                   fwdInput,
                   fwdProbeStart,
                   probeState,
                   budget,
                   [fwdProbeStart](std::size_t endPos) {
                     return endPos > fwdProbeStart;
                   });
          if (canForward) {
            return false;
          }
        }
      }
    }

    // After consuming maximum iterations, try continuation once
    if (count >= MinR) {
      return cont(pos);
    }
    return false;
  }
};

template <
    const auto& Ast,
    const auto& ForwardAst,
    Flags F,
    bool TrackCaptures,
    int NumGroups,
    bool UseBudget,
    Direction Dir = Direction::Forward>
struct BacktrackRunner {
  static MatchOutcome<NumGroups> matchAnchored(InputView<Dir> input) noexcept {
    MatchOutcome<NumGroups> outcome;
    MatchState<NumGroups> state;
    std::size_t budget = UseBudget
        ? input.size() * static_cast<std::size_t>(Ast.node_count) * 8 + 1024
        : static_cast<std::size_t>(-1);

    std::size_t budgetCopy = budget;

    if constexpr (Ast.leading_dot_star_min >= 0) {
      // Leading dot-star pruned — delegate to search starting at min.
      std::size_t minSkip = static_cast<std::size_t>(Ast.leading_dot_star_min);
      for (std::size_t s = input.startPos(); !input.scanDone(s);
           s = (Dir == Direction::Forward ? s + 1 : s - 1)) {
        if constexpr (Dir == Direction::Forward) {
          if (s < minSkip) {
            continue;
          }
        } else {
          if (input.size() - s < minSkip) {
            continue;
          }
        }
        budgetCopy = budget;
        auto result = tryMatchAt(input, s, budgetCopy);
        if (result.status == MatchStatus::Matched) {
          outcome.status = MatchStatus::Matched;
          if constexpr (TrackCaptures) {
            outcome.state = result.state;
            outcome.state.groups[0] = {0, input.size()};
          }
          return outcome;
        }
        if (UseBudget && budgetCopy == 0) {
          outcome.status = MatchStatus::BudgetExhausted;
          return outcome;
        }
      }
      return outcome;
    } else {
      std::size_t startPos = input.startPos();
      auto trailingCont = [&](std::size_t endPos) {
        if constexpr (Ast.trailing_dot_star_min >= 0) {
          if (endPos <= input.size()) {
            auto extended = computeDotStarExtension<Dir>(
                input,
                endPos,
                Ast.trailing_dot_star_dot_all,
                Ast.trailing_dot_star_anchor);
            return extended != std::string_view::npos;
          }
          return false;
        } else {
          if constexpr (Dir == Direction::Forward) {
            return endPos == input.size();
          } else {
            return endPos == 0;
          }
        }
      };

      bool matched = BacktrackExecutor<
          Ast,
          ForwardAst,
          F,
          Ast.root,
          TrackCaptures,
          NumGroups,
          /*AllowSlide=*/false,
          Dir>::match(input, startPos, state, budgetCopy, trailingCont);

      if (matched) {
        outcome.status = MatchStatus::Matched;
        if constexpr (TrackCaptures) {
          state.groups[0] = {0, input.size()};
        }
        outcome.state = state;
      } else if (UseBudget && budgetCopy == 0) {
        outcome.status = MatchStatus::BudgetExhausted;
      } else {
        outcome.status = MatchStatus::NoMatch;
      }
      return outcome;
    }
  }

  // Try matching starting at position `start`, accepting any match length.
  // Uses a shared budget reference so the caller can track total budget
  // across multiple positions. Returns a MatchOutcome with groups[0] set
  // to the match span.
  static MatchOutcome<NumGroups> tryMatchAt(
      InputView<Dir> input, std::size_t start, std::size_t& budget) noexcept {
    MatchOutcome<NumGroups> outcome;
    MatchState<NumGroups> state;
    std::size_t matchEnd = std::string_view::npos;

    bool matched = BacktrackExecutor<
        Ast,
        ForwardAst,
        F,
        Ast.root,
        TrackCaptures,
        NumGroups,
        /*AllowSlide=*/true,
        Dir>::match(input, start, state, budget, [&](std::size_t endPos) {
      matchEnd = endPos;
      return true;
    });

    if (matched) {
      // Extend match boundary for trailing dot-star.
      if constexpr (Ast.trailing_dot_star_min >= 0) {
        if constexpr (Dir == Direction::Forward) {
          auto extended = computeDotStarExtension<Dir>(
              input,
              matchEnd,
              Ast.trailing_dot_star_dot_all,
              Ast.trailing_dot_star_anchor);
          if (extended == std::string_view::npos) {
            if (UseBudget && budget == 0) {
              outcome.status = MatchStatus::BudgetExhausted;
            }
            return outcome;
          }
          matchEnd = extended;
        } else {
          auto extended = computeDotStarExtension<Dir>(
              input,
              matchEnd,
              Ast.trailing_dot_star_dot_all,
              Ast.trailing_dot_star_anchor);
          if (extended == std::string_view::npos) {
            if (UseBudget && budget == 0) {
              outcome.status = MatchStatus::BudgetExhausted;
            }
            return outcome;
          }
          matchEnd = extended;
        }
      }
      // Extend match START for leading dot-star.
      if constexpr (Ast.leading_dot_star_min >= 0) {
        if constexpr (Dir == Direction::Forward) {
          auto extended = computeLeadingDotStarExtension<Dir>(
              input,
              start,
              Ast.leading_dot_star_dot_all,
              Ast.leading_dot_star_anchor);
          if (extended == std::string_view::npos) {
            if (UseBudget && budget == 0) {
              outcome.status = MatchStatus::BudgetExhausted;
            }
            return outcome;
          }
          start = extended;
        } else {
          auto extended = computeLeadingDotStarExtension<Dir>(
              input,
              start,
              Ast.leading_dot_star_dot_all,
              Ast.leading_dot_star_anchor);
          if (extended == std::string_view::npos) {
            if (UseBudget && budget == 0) {
              outcome.status = MatchStatus::BudgetExhausted;
            }
            return outcome;
          }
          start = extended;
        }
      }
      outcome.status = MatchStatus::Matched;
      if constexpr (TrackCaptures) {
        if constexpr (Dir == Direction::Forward) {
          std::size_t actualStart =
              state.groups[0].offset != std::string_view::npos
              ? state.groups[0].offset
              : start;
          state.groups[0] = {actualStart, matchEnd - actualStart};
        } else {
          std::size_t actualStart = matchEnd;
          state.groups[0] = {actualStart, start - actualStart};
        }
      }
      outcome.state = state;
    } else if (UseBudget && budget == 0) {
      outcome.status = MatchStatus::BudgetExhausted;
    }
    return outcome;
  }
};

} // namespace folly::regex::detail
