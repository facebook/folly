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
#include <cstddef>
#include <cstring>
#include <string_view>
#include <utility>

#include <folly/regex/Flags.h>
#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/AstConcepts.h>
#include <folly/regex/detail/CharClass.h>
#include <folly/regex/detail/Direction.h>
#include <folly/regex/detail/Executor.h>
#include <folly/regex/detail/Nfa.h>

namespace folly::regex::detail {

// Deduplication table with compile-time strategy selection.
// For small sizes (≤ kThreshold bits), uses a FixedBitset with O(words) clear.
// For large sizes, uses generation counters with O(1) clear (just a bump).
template <int Size>
struct DedupTable {
  static constexpr int kThreshold = 1024;
  static constexpr bool kUseBitset = (Size <= kThreshold);

  FixedBitset<kUseBitset ? Size : 1> bits_ = {};
  int generation_[kUseBitset ? 1 : Size] = {};
  int currentGen_ = 1;

  void reset() noexcept {
    if constexpr (kUseBitset) {
      bits_.clearAll();
    } else {
      if (currentGen_ >= 2147483647) {
        for (int i = 0; i < Size; ++i) {
          generation_[i] = 0;
        }
        currentGen_ = 1;
      } else {
        ++currentGen_;
      }
    }
  }

  bool test(int idx) const noexcept {
    if (idx < 0 || idx >= Size) {
      return true;
    }
    if constexpr (kUseBitset) {
      return bits_.test(idx);
    } else {
      return generation_[idx] == currentGen_;
    }
  }

  bool testAndSet(int idx) noexcept {
    if (idx < 0 || idx >= Size) {
      return true;
    }
    if constexpr (kUseBitset) {
      if (bits_.test(idx)) {
        return true;
      }
      bits_.set(idx);
      return false;
    } else {
      if (generation_[idx] == currentGen_) {
        return true;
      }
      generation_[idx] = currentGen_;
      return false;
    }
  }
};

// Intrusive linked-list node wrapping a thread.
template <typename T>
struct ThreadNode {
  T thread;
  ThreadNode* next_in_list = nullptr;
};

// Singly-linked list of thread nodes with O(1) append and O(1) popFront.
template <typename T>
struct ActiveList {
  using Node = ThreadNode<T>;
  Node* head = nullptr;
  Node* tail = nullptr;
  int count = 0;

  void append(Node* n) noexcept {
    n->next_in_list = nullptr;
    if (tail) {
      tail->next_in_list = n;
    } else {
      head = n;
    }
    tail = n;
    ++count;
  }

  void clear() noexcept {
    head = nullptr;
    tail = nullptr;
    count = 0;
  }
};

// Arena-based pool allocator for ThreadNode objects.
// Allocates from a contiguous array, with a free list for recycled nodes.
template <typename T, int PoolSize>
struct ThreadPool {
  using Node = ThreadNode<T>;
  Node nodes[PoolSize > 0 ? PoolSize : 1] = {};
  int nextFree = 0;
  Node* freeList = nullptr;

  Node* allocate() noexcept {
    if (freeList) {
      auto* n = freeList;
      freeList = n->next_in_list;
      return n;
    }
    if (nextFree < PoolSize) {
      return &nodes[nextFree++];
    }
    return nullptr;
  }

  void release(Node* n) noexcept {
    n->next_in_list = freeList;
    freeList = n;
  }

  // Splice an entire ActiveList onto the free list in O(1).
  void releaseAll(ActiveList<T>& list) noexcept {
    if (!list.head) {
      return;
    }
    list.tail->next_in_list = freeList;
    freeList = list.head;
    list.clear();
  }
};

template <bool HasPossessive>
struct PossessiveFields {
  int possessive_origin = -1;
  bool possessive_is_body = false;
  std::size_t possessive_entry_pos = SIZE_MAX;
  int possessive_probe_id = -1;
  bool possessive_at_max = false;
};

template <>
struct PossessiveFields<false> {};

template <int NumGroups, int MaxCounters, bool HasPossessive = false>
struct NfaThread : PossessiveFields<HasPossessive> {
  int state = -1;
  std::array<GroupSpan, NumGroups + 1> groups = {};
  int counters[MaxCounters] = {};
};

template <bool HasPossessive>
struct NfaThread<-1, 0, HasPossessive> : PossessiveFields<HasPossessive> {
  int state = -1;
};

template <int NumGroups, bool HasPossessive>
struct NfaThread<NumGroups, 0, HasPossessive>
    : PossessiveFields<HasPossessive> {
  int state = -1;
  std::array<GroupSpan, NumGroups + 1> groups = {};
};

template <int MaxCounters, bool HasPossessive>
struct NfaThread<-1, MaxCounters, HasPossessive>
    : PossessiveFields<HasPossessive> {
  int state = -1;
  int counters[MaxCounters] = {};
};

// Thread type for position-only searching (no capture groups).
template <int MaxCounters, bool HasPossessive = false>
struct PosThread : PossessiveFields<HasPossessive> {
  int state = -1;
  std::size_t startPos = 0;
  int counters[MaxCounters] = {};
};

template <bool HasPossessive>
struct PosThread<0, HasPossessive> : PossessiveFields<HasPossessive> {
  int state = -1;
  std::size_t startPos = 0;
};

// Policy for NfaRunner: tracks capture groups when TrackCaptures is true.
template <
    const auto& Prog,
    bool TrackCaptures,
    int NumGroups,
    Direction Dir = Direction::Forward>
struct NfaRunnerPolicy {
  static constexpr int kMaxCounters = Prog.num_counters;
  static constexpr bool kHasPossessive = Prog.has_possessive;
  using Thread =
      NfaThread<TrackCaptures ? NumGroups : -1, kMaxCounters, kHasPossessive>;

  static void onGroupStart(
      Thread& t, const NfaState& s, std::size_t pos) noexcept {
    if constexpr (TrackCaptures) {
      t.groups[s.group_id].offset = pos;
      t.groups[s.group_id].length = 0;
    }
  }
  static void onGroupEnd(
      Thread& t, const NfaState& s, std::size_t pos) noexcept {
    if constexpr (TrackCaptures) {
      if constexpr (Dir == Direction::Reverse) {
        t.groups[s.group_id].length = t.groups[s.group_id].offset - pos;
        t.groups[s.group_id].offset = pos;
      } else {
        t.groups[s.group_id].length = pos - t.groups[s.group_id].offset;
      }
    }
  }
};

// Policy for NfaPositionSearcher: no capture group tracking.
template <const auto& Prog>
struct NfaPositionSearcherPolicy {
  static constexpr int kMaxCounters = Prog.num_counters;
  static constexpr bool kHasPossessive = Prog.has_possessive;
  using Thread = PosThread<kMaxCounters, kHasPossessive>;

  static void onGroupStart(Thread&, const NfaState&, std::size_t) noexcept {}
  static void onGroupEnd(Thread&, const NfaState&, std::size_t) noexcept {}
};

// Shared NFA execution logic for both NfaRunner and NfaPositionSearcher.
// PolicyT provides the Thread type and group start/end callbacks.
template <
    typename PolicyT,
    const auto& Prog,
    const auto& Ast,
    Flags F,
    Direction Dir,
    const auto& ForwardAst,
    const auto& LookaroundProbeStore = ForwardAst>
struct NfaExecutorBase {
  using Thread = typename PolicyT::Thread;

  static constexpr int kMaxStates = Prog.state_count;
  static constexpr int kMaxCounters = Prog.num_counters;
  static constexpr int kMaxRepeatValue = Prog.max_repeat_value;
  static constexpr bool kHasPossessive = Prog.has_possessive;

  // When counters are active, the dedup key must include the full counter
  // tuple, not just individual counter values.  Two threads at the same
  // state with different counter configurations represent genuinely
  // different matching contexts (e.g., iteration 1 vs iteration 2 of an
  // outer counted repeat).  We linearize the counter tuple into a flat
  // index: hash = c[0]*(M+1)^(N-1) + c[1]*(M+1)^(N-2) + ... + c[N-1],
  // where M = kMaxRepeatValue and N = kMaxCounters.
  static constexpr int kCombinedCounterSize = [] {
    int size = 1;
    for (int i = 0; i < kMaxCounters; ++i) {
      size *= (kMaxRepeatValue + 1);
    }
    return size;
  }();
  static constexpr int kInListSize =
      kMaxStates > 0 ? kMaxStates* kCombinedCounterSize : 1;
  static constexpr int kPoolSize = kInListSize > 0 ? kInListSize * 4 : 1;

  using Node = ThreadNode<Thread>;
  using List = ActiveList<Thread>;
  using Pool = ThreadPool<Thread, kPoolSize>;
  using Dedup = DedupTable<(kInListSize > 0 ? kInListSize : 1)>;
  using CounterSeen =
      FixedBitset<(kCombinedCounterSize > 0 ? kCombinedCounterSize : 1)>;

  struct BuildContext {
    List& list;
    Dedup& dedup;
    CounterSeen& counterSeen;
    Pool& pool;
  };

  static int counterTupleHash([[maybe_unused]] const Thread& t) {
    if constexpr (kMaxCounters == 0) {
      return 0;
    } else {
      int hash = 0;
      for (int i = 0; i < kMaxCounters; ++i) {
        hash = hash * (kMaxRepeatValue + 1) + t.counters[i];
      }
      return hash;
    }
  }

  static int stateCounterIndex(const Thread& t) {
    if constexpr (kMaxCounters == 0) {
      return t.state;
    } else {
      return t.state * kCombinedCounterSize + counterTupleHash(t);
    }
  }

  static void addThread(BuildContext& ctx, Thread t) {
    auto* node = ctx.pool.allocate();
    if (node) {
      node->thread = t;
      ctx.list.append(node);
    }
  }

  static void addCounted(BuildContext& ctx, Thread t) {
    if constexpr (kMaxCounters > 0) {
      int hash = counterTupleHash(t);
      if (ctx.counterSeen.test(hash)) {
        return;
      }
      ctx.counterSeen.set(hash);
      auto* node = ctx.pool.allocate();
      if (node) {
        node->thread = t;
        ctx.list.append(node);
      }
    }
  }

  static void propagatePossessive(
      Thread& dest,
      const NfaState& s,
      int originState,
      bool isBody,
      std::size_t pos,
      int probeId = -1) noexcept {
    if constexpr (kHasPossessive) {
      if (s.isPossessive()) {
        dest.possessive_origin = originState;
        dest.possessive_is_body = isBody;
        if (dest.possessive_entry_pos == SIZE_MAX) {
          dest.possessive_entry_pos = pos;
          if (probeId >= 0) {
            dest.possessive_probe_id = probeId;
          }
        }
      }
    }
  }

  static void addState(
      BuildContext& ctx, Thread t, InputView<Dir> input, std::size_t pos) {
    if (t.state < 0 || t.state >= Prog.state_count) {
      return;
    }
    if (ctx.dedup.testAndSet(stateCounterIndex(t))) {
      return;
    }

    const auto& s = Prog.states[t.state];

    switch (s.kind) {
      case NfaStateKind::Split: {
        if constexpr (Prog.has_discriminators) {
          if (s.discriminator_offset >= 0) {
            std::size_t peekPos;
            bool peekValid;
            if constexpr (Dir == Direction::Forward) {
              peekPos = pos + s.discriminator_offset;
              peekValid = peekPos < input.size();
            } else {
              peekValid =
                  pos >= static_cast<std::size_t>(s.discriminator_offset) + 1;
              peekPos = peekValid ? pos - 1 - s.discriminator_offset : 0;
            }
            if (peekValid) {
              auto peekChar = static_cast<unsigned char>(input[peekPos]);
              auto matchesChar = [&](int charField) -> bool {
                if (charField < 0) {
                  return true;
                }
                if (charField < 256) {
                  return peekChar == static_cast<unsigned char>(charField);
                }
                return Ast.charClassTestAt(
                    charField - 256, static_cast<char>(peekChar));
              };
              int cur = t.state;
              while (cur >= 0) {
                const auto& ss = Prog.states[cur];
                if (ss.kind != NfaStateKind::Split) {
                  break;
                }
                if (ss.alt >= 0 && matchesChar(ss.discriminator_alt_char)) {
                  Thread tAlt = t;
                  tAlt.state = ss.alt;
                  addState(ctx, tAlt, input, pos);
                }
                if (ss.discriminator_next_char >= 0) {
                  if (ss.next >= 0 && matchesChar(ss.discriminator_next_char)) {
                    Thread tNext = t;
                    tNext.state = ss.next;
                    addState(ctx, tNext, input, pos);
                  }
                  break;
                }
                // discriminator_next_char unresolved (-1). If next is
                // another Split, continue the chain. Otherwise this is
                // the innermost split — add the branch unconditionally.
                if (ss.next < 0 ||
                    Prog.states[ss.next].kind != NfaStateKind::Split) {
                  if (ss.next >= 0) {
                    Thread tNext = t;
                    tNext.state = ss.next;
                    addState(ctx, tNext, input, pos);
                  }
                  break;
                }
                cur = ss.next;
              }
              return;
            }
          }
        }
        Thread t1 = t;
        t1.state = s.next;
        propagatePossessive(t1, s, t.state, true, pos);
        addState(ctx, t1, input, pos);
        Thread t2 = t;
        t2.state = s.alt;
        propagatePossessive(t2, s, t.state, false, pos);
        addState(ctx, t2, input, pos);
        return;
      }
      case NfaStateKind::CountedRepeat: {
        if constexpr (kMaxCounters > 0) {
          int cv = t.counters[s.counter_id];
          if (cv < s.min_repeat) {
            Thread tLoop = t;
            tLoop.counters[s.counter_id] = cv + 1;
            tLoop.state = s.alt;
            propagatePossessive(
                tLoop, s, t.state, true, pos, s.possessive_probe_idx);
            addCounted(ctx, tLoop);
            addState(ctx, tLoop, input, pos);
          } else if (s.max_repeat >= 0 && cv >= s.max_repeat) {
            Thread tExit = t;
            tExit.counters[s.counter_id] = 0;
            tExit.state = s.next;
            if constexpr (kHasPossessive) {
              if (s.isPossessive()) {
                tExit.possessive_at_max = true;
              }
            }
            // Forward probe check for possessive repeats at exit.
            // If the forward probe from the current position (where the
            // reverse match ended) consumes more than this thread consumed,
            // reject this exit — the forward engine would have matched more.
            if constexpr (kHasPossessive && Dir == Direction::Reverse) {
              if (tExit.possessive_probe_id >= 0 && s.min_repeat == 1 &&
                  s.max_repeat == 1) {
                std::size_t bodyConsumed = tExit.possessive_entry_pos > pos
                    ? tExit.possessive_entry_pos - pos
                    : pos - tExit.possessive_entry_pos;
                int probeConsumed = runForwardProbe(
                    tExit.possessive_probe_id,
                    input.template fullInput<Direction::Forward>(),
                    input.posInFull(pos));
                if (probeConsumed >= 0 &&
                    static_cast<std::size_t>(probeConsumed) > bodyConsumed) {
                  // Don't add this exit — forward match would consume more
                } else {
                  addState(ctx, tExit, input, pos);
                }
              } else {
                addState(ctx, tExit, input, pos);
              }
            } else {
              addState(ctx, tExit, input, pos);
            }
          } else {
            // For unbounded repeats the decision is always "can loop
            // or exit" once cv >= min_repeat, regardless of the exact
            // value.  Cap the counter so counterSeen dedup collapses
            // all optional iterations into one entry per position.
            int nextCv = (s.max_repeat < 0) ? s.min_repeat : (cv + 1);
            if (s.repeat_mode != RepeatMode::Lazy) {
              Thread tLoop = t;
              tLoop.counters[s.counter_id] = nextCv;
              tLoop.state = s.alt;
              propagatePossessive(tLoop, s, t.state, true, pos);
              addCounted(ctx, tLoop);
              addState(ctx, tLoop, input, pos);
              Thread tExit = t;
              tExit.counters[s.counter_id] = 0;
              tExit.state = s.next;
              propagatePossessive(tExit, s, t.state, false, pos);
              addState(ctx, tExit, input, pos);
            } else {
              Thread tExit = t;
              tExit.counters[s.counter_id] = 0;
              tExit.state = s.next;
              addState(ctx, tExit, input, pos);
              Thread tLoop = t;
              tLoop.counters[s.counter_id] = nextCv;
              tLoop.state = s.alt;
              addCounted(ctx, tLoop);
              addState(ctx, tLoop, input, pos);
            }
          }
        }
        return;
      }
      case NfaStateKind::GroupStart: {
        PolicyT::onGroupStart(t, s, pos);
        t.state = s.next;
        addState(ctx, t, input, pos);
        return;
      }
      case NfaStateKind::GroupEnd: {
        PolicyT::onGroupEnd(t, s, pos);
        t.state = s.next;
        addState(ctx, t, input, pos);
        return;
      }
      case NfaStateKind::AnchorBegin: {
        if (pos == 0) {
          t.state = s.next;
          addState(ctx, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEnd: {
        if (pos == input.size()) {
          t.state = s.next;
          addState(ctx, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorStartOfString: {
        if (pos == 0) {
          t.state = s.next;
          addState(ctx, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEndOfString: {
        if (pos == input.size()) {
          t.state = s.next;
          addState(ctx, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEndOfStringOrNewline: {
        if (pos == input.size() ||
            (pos + 1 == input.size() && input[pos] == '\n')) {
          t.state = s.next;
          addState(ctx, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorBeginLine: {
        if (pos == 0 || input[pos - 1] == '\n') {
          t.state = s.next;
          addState(ctx, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEndLine: {
        if (pos == input.size() || input[pos] == '\n') {
          t.state = s.next;
          addState(ctx, t, input, pos);
        }
        return;
      }
      case NfaStateKind::Match:
      case NfaStateKind::Literal:
      case NfaStateKind::AnyByte:
      case NfaStateKind::CharClass:
        addThread(ctx, t);
        return;
      case NfaStateKind::LookaheadProbe: {
        // Forward probe — always forward, independent of main match direction
        if (runProbeTest(s.probe_id, input, pos)) {
          t.state = s.next;
          addState(ctx, t, input, pos);
        }
        return;
      }
      case NfaStateKind::NegLookaheadProbe: {
        if (!runProbeTest(s.probe_id, input, pos)) {
          t.state = s.next;
          addState(ctx, t, input, pos);
        }
        return;
      }
      case NfaStateKind::LookbehindProbe: {
        if (s.lookbehind_width >= 0 && isLiteralLookbehindProbe(s.probe_id)) {
          // Fast path: single literal child, use memcmp.
          int width = s.lookbehind_width;
          std::size_t fullPos = input.posInFull(pos);
          if (fullPos >= static_cast<std::size_t>(width)) {
            if (evaluateLiteralLookbehind(s.probe_id, input, pos, width)) {
              t.state = s.next;
              addState(ctx, t, input, pos);
            }
          }
        } else {
          // Variable-width: reverse probe
          if (runProbeTest(s.probe_id, input, pos)) {
            t.state = s.next;
            addState(ctx, t, input, pos);
          }
        }
        return;
      }
      case NfaStateKind::NegLookbehindProbe: {
        if (s.lookbehind_width >= 0 && isLiteralLookbehindProbe(s.probe_id)) {
          // Fast path: single literal child, use memcmp.
          int width = s.lookbehind_width;
          std::size_t fullPos = input.posInFull(pos);
          if (fullPos < static_cast<std::size_t>(width)) {
            // Not enough input — lookbehind can't match, negative succeeds.
            t.state = s.next;
            addState(ctx, t, input, pos);
          } else {
            if (!evaluateLiteralLookbehind(s.probe_id, input, pos, width)) {
              t.state = s.next;
              addState(ctx, t, input, pos);
            }
          }
        } else {
          if (!runProbeTest(s.probe_id, input, pos)) {
            t.state = s.next;
            addState(ctx, t, input, pos);
          }
        }
        return;
      }
    }
  }

  static bool stateMatches(const NfaState& s, char c) {
    if (Prog.partition.valid) {
      int iv = Prog.partition.charToInterval(static_cast<unsigned char>(c));
      return (s.interval_mask >> iv) & 1;
    }
    switch (s.kind) {
      case NfaStateKind::Literal:
        return c == s.ch;
      case NfaStateKind::AnyByte:
        return true;
      case NfaStateKind::CharClass:
        return Ast.charClassTestAt(s.char_class_index, c);
      case NfaStateKind::Match:
      case NfaStateKind::Split:
      case NfaStateKind::CountedRepeat:
      case NfaStateKind::AnchorBegin:
      case NfaStateKind::AnchorEnd:
      case NfaStateKind::AnchorStartOfString:
      case NfaStateKind::AnchorEndOfString:
      case NfaStateKind::AnchorEndOfStringOrNewline:
      case NfaStateKind::AnchorBeginLine:
      case NfaStateKind::AnchorEndLine:
      case NfaStateKind::GroupStart:
      case NfaStateKind::GroupEnd:
      case NfaStateKind::LookaheadProbe:
      case NfaStateKind::NegLookaheadProbe:
      case NfaStateKind::LookbehindProbe:
      case NfaStateKind::NegLookbehindProbe:
        return false;
    }
  }

  // Check if the probe's root is a single literal node.
  static bool isLiteralLookbehindProbe(int probeId) noexcept {
    if (probeId < 0 || !LookaroundProbeStore.hasProbe(probeId)) {
      return false;
    }
    int rootIdx = LookaroundProbeStore.probes[probeId].root;
    return rootIdx >= 0 &&
        LookaroundProbeStore.nodes[rootIdx].kind == NodeKind::Literal;
  }

  // Evaluate a fixed-width lookbehind that has a single literal child.
  // Uses memcmp for fast comparison against the input.
  static bool evaluateLiteralLookbehind(
      int probeId, InputView<Dir> input, std::size_t pos, int width) noexcept {
    int rootIdx = LookaroundProbeStore.probes[probeId].root;
    auto lit = LookaroundProbeStore.nodes[rootIdx].literal;
    std::size_t fullPos = input.posInFull(pos);
    std::size_t start = fullPos - static_cast<std::size_t>(width);
    return std::memcmp(
               input.original_input.data() + start, lit.data(), lit.size()) ==
        0;
  }

  // Anchored existence test from a custom start state/position.
  // Returns true if any match exists (short-circuits on first Match state).
  static bool anchoredTestFrom(
      InputView<Dir> input, std::size_t startPos, int startState) noexcept {
    struct Cb {
      std::size_t startPos_;
      int startState_;
      bool matched = false;

      std::size_t startPosition(InputView<Dir>) { return startPos_; }
      Thread seedThread() {
        Thread t;
        t.state = startState_;
        return t;
      }
      Thread positionThread(std::size_t) { return Thread{}; }
      StepAction onMatch(const Thread&, std::size_t, int) {
        matched = true;
        return StepAction::Return;
      }
      void onAdvance(int) {}
      bool shouldTerminate(std::size_t, InputView<Dir>, const List&) {
        return false;
      }
      bool result() { return matched; }
    };
    Cb cb{startPos, startState};
    return stepLoop(input, cb);
  }

  // Run a lookaround probe test using an embedded sub-NFA.
  // Lookahead probes always run forward; lookbehind probes always run reverse.
  static bool runProbeTest(
      int probeId, InputView<Dir> input, std::size_t pos) noexcept {
    if (probeId < 0 || probeId >= Prog.lookaround_probe_count) {
      return false;
    }
    int probeStart = Prog.lookaround_probe_start[probeId];
    if (probeStart < 0) {
      return false;
    }

    std::size_t fullPos = input.posInFull(pos);

    // Check probe direction: lookahead runs forward, lookbehind runs reverse.
    if (LookaroundProbeStore.probes[probeId].dir == Direction::Reverse) {
      // Reverse probe: run from fullPos leftward
      auto revInput = input.template fullInput<Direction::Reverse>();
      using RevProbeBase = NfaExecutorBase<
          NfaPositionSearcherPolicy<Prog>,
          Prog,
          LookaroundProbeStore,
          F,
          Direction::Reverse,
          LookaroundProbeStore,
          LookaroundProbeStore>;
      return RevProbeBase::anchoredTestFrom(revInput, fullPos, probeStart);
    } else {
      // Forward probe: run from fullPos rightward
      auto fullInput = input.template fullInput<Direction::Forward>();
      using FwdProbeBase = NfaExecutorBase<
          NfaPositionSearcherPolicy<Prog>,
          Prog,
          LookaroundProbeStore,
          F,
          Direction::Forward,
          LookaroundProbeStore,
          LookaroundProbeStore>;
      return FwdProbeBase::anchoredTestFrom(fullInput, fullPos, probeStart);
    }
  }

  enum class StepAction { Continue, Return };

  // Unified NFA step loop. Callbacks provide variant-specific behavior:
  //   startPosition(input) -> std::size_t
  //   seedThread()         -> Thread (state<0 = no injection)
  //   positionThread(i)    -> Thread (state<0 = no injection)
  //   onMatch(t, pos, j)   -> StepAction
  //   onAdvance(j)         -> void
  //   shouldTerminate(pos, input, nextList) -> bool
  //   result()             -> ResultType
  template <typename Callbacks>
  static auto stepLoop(InputView<Dir> input, Callbacks& cb) noexcept
      -> decltype(cb.result()) {
    Pool pool;
    List list0, list1;
    Dedup dedup0, dedup1;
    CounterSeen counterSeen0{}, counterSeen1{};

    auto* current = &list0;
    auto* next = &list1;
    auto* currentDedup = &dedup0;
    auto* nextDedup = &dedup1;
    auto* currentCounterSeen = &counterSeen0;
    auto* nextCounterSeen = &counterSeen1;

    std::size_t start = cb.startPosition(input);

    {
      BuildContext curCtx{*current, *currentDedup, *currentCounterSeen, pool};
      if (auto t = cb.seedThread(); t.state >= 0) {
        addState(curCtx, t, input, start);
      }
    }

    for (std::size_t i = start;; i = InputView<Dir>::advance(i)) {
      {
        BuildContext curCtx{*current, *currentDedup, *currentCounterSeen, pool};
        if (auto t = cb.positionThread(i); t.state >= 0) {
          addState(curCtx, t, input, i);
        }
      }

      [[maybe_unused]] FixedBitset<(kMaxStates > 0 ? kMaxStates : 1)>
          possessiveBodyMatched;

      BuildContext nextCtx{*next, *nextDedup, *nextCounterSeen, pool};

      int j = 0;
      for (auto* node = current->head; node; node = node->next_in_list, ++j) {
        const auto& t = node->thread;
        const auto& s = Prog.states[t.state];

        if (s.kind == NfaStateKind::Match) {
          if (cb.onMatch(t, i, j) == StepAction::Return) {
            return cb.result();
          }
          continue;
        }

        if constexpr (kHasPossessive) {
          if (t.possessive_origin >= 0 && !t.possessive_is_body &&
              possessiveBodyMatched.test(t.possessive_origin)) {
            continue;
          }
        }

        if (input.canConsume(i) && stateMatches(s, input.charAt(i))) {
          if constexpr (kHasPossessive) {
            if (t.possessive_origin >= 0 && t.possessive_is_body) {
              possessiveBodyMatched.set(t.possessive_origin);
            }
          }
          cb.onAdvance(j);
          Thread nt = t;
          nt.state = s.next;
          addState(nextCtx, nt, input, InputView<Dir>::advance(i));
        }
      }

      if (cb.shouldTerminate(i, input, *next)) {
        return cb.result();
      }

      if (input.atEnd(i)) {
        break;
      }

      pool.releaseAll(*current);
      std::swap(current, next);
      std::swap(currentDedup, nextDedup);
      std::swap(currentCounterSeen, nextCounterSeen);
      nextDedup->reset();
      if constexpr (kMaxCounters > 0) {
        nextCounterSeen->clearAll();
      }
    }

    return cb.result();
  }

  // Anchored greedy match from a custom start state/position.
  // Returns number of characters consumed, or -1 if no match.
  static int anchoredGreedyFrom(
      InputView<Dir> input, std::size_t startPos, int startState) noexcept {
    struct Cb {
      std::size_t startPos_;
      int startState_;
      int best = -1;

      std::size_t startPosition(InputView<Dir>) { return startPos_; }
      Thread seedThread() {
        Thread t;
        t.state = startState_;
        return t;
      }
      Thread positionThread(std::size_t) { return Thread{}; }
      StepAction onMatch(const Thread&, std::size_t pos, int) {
        best = static_cast<int>(pos - startPos_);
        return StepAction::Continue;
      }
      void onAdvance(int) {}
      bool shouldTerminate(std::size_t, InputView<Dir>, const List&) {
        return false;
      }
      int result() { return best; }
    };
    Cb cb{startPos, startState};
    return stepLoop(input, cb);
  }

  // Run a forward probe sub-NFA from position `startPos`. Returns the number
  // of characters consumed by the greedy match, or -1 if no match.
  // Uses the shared stepLoop with ForwardAst for correct char class testing,
  // counter tracking, possessive filtering, and anchor evaluation.
  static int runForwardProbe(
      int probeIdx,
      InputView<Direction::Forward> input,
      std::size_t startPos) noexcept {
    if (probeIdx < 0 || probeIdx >= Prog.probe_count) {
      return -1;
    }
    int probeStart = Prog.probe_start[probeIdx];
    if (probeStart < 0) {
      return -1;
    }

    using ProbeBase = NfaExecutorBase<
        NfaPositionSearcherPolicy<Prog>,
        Prog,
        ForwardAst,
        F,
        Direction::Forward,
        ForwardAst>;
    return ProbeBase::anchoredGreedyFrom(input, startPos, probeStart);
  }
};

template <
    const auto& Prog,
    const auto& Ast,
    Flags F,
    bool TrackCaptures,
    int NumGroups,
    Direction Dir = Direction::Forward,
    const auto& ForwardAst = Ast,
    const auto& LookaroundProbeStore = ForwardAst>
struct NfaRunner
    : NfaExecutorBase<
          NfaRunnerPolicy<Prog, TrackCaptures, NumGroups, Dir>,
          Prog,
          Ast,
          F,
          Dir,
          ForwardAst,
          LookaroundProbeStore> {
  using Base = NfaExecutorBase<
      NfaRunnerPolicy<Prog, TrackCaptures, NumGroups, Dir>,
      Prog,
      Ast,
      F,
      Dir,
      ForwardAst,
      LookaroundProbeStore>;
  using Base::kHasPossessive;
  using Base::kMaxStates;
  using Base::runForwardProbe;
  using typename Base::BuildContext;
  using typename Base::List;
  using typename Base::Thread;
  using StepAction = typename Base::StepAction;

  static MatchOutcome<NumGroups> matchAnchored(InputView<Dir> input) {
    if constexpr (Ast.leading_dot_star_min >= 0) {
      // Leading dot-star pruned — use search to find body at any position.
      auto result = search(input);
      if (result.status == MatchStatus::Matched) {
        if constexpr (TrackCaptures) {
          result.state.groups[0] = {0, input.size()};
        }
      }
      return result;
    }
    struct Cb {
      InputView<Dir> input;
      MatchOutcome<NumGroups> bestMatch;

      std::size_t startPosition(InputView<Dir> in) { return in.startPos(); }
      Thread seedThread() {
        Thread t;
        t.state = Prog.start_state;
        return t;
      }
      Thread positionThread(std::size_t) { return Thread{}; }
      StepAction onMatch(const Thread& t, std::size_t pos, int) {
        if constexpr (Ast.trailing_dot_star_min >= 0) {
          // Trailing dot-star: accept match at any position,
          // extending analytically.
          if constexpr (Dir == Direction::Forward) {
            auto extended = computeDotStarExtension<Direction::Forward>(
                input,
                pos,
                Ast.trailing_dot_star_dot_all,
                Ast.trailing_dot_star_anchor);
            if (extended == std::string_view::npos) {
              return StepAction::Continue;
            }
          } else {
            auto extended = computeDotStarExtension<Direction::Reverse>(
                input,
                pos,
                Ast.trailing_dot_star_dot_all,
                Ast.trailing_dot_star_anchor);
            if (extended == std::string_view::npos) {
              return StepAction::Continue;
            }
          }
        } else {
          if (!input.atEnd(pos)) {
            return StepAction::Continue;
          }
        }
        if constexpr (kHasPossessive && Dir == Direction::Reverse) {
          if (t.possessive_origin >= 0 && !t.possessive_is_body &&
              !t.possessive_at_max) {
            const auto& ps = Prog.states[t.possessive_origin];
            if (ps.possessive_probe_idx >= 0 &&
                t.possessive_entry_pos != SIZE_MAX) {
              if (runForwardProbe(
                      ps.possessive_probe_idx,
                      input.template fullInput<Direction::Forward>(),
                      input.posInFull(t.possessive_entry_pos)) > 0) {
                return StepAction::Continue;
              }
            }
          }
        }
        bestMatch.status = MatchStatus::Matched;
        if constexpr (TrackCaptures) {
          bestMatch.state.groups = t.groups;
          bestMatch.state.groups[0] = {0, input.size()};
        }
        return StepAction::Return;
      }
      void onAdvance(int) {}
      bool shouldTerminate(std::size_t pos, InputView<Dir> in, const List&) {
        return in.atEnd(pos);
      }
      MatchOutcome<NumGroups> result() { return bestMatch; }
    };
    Cb cb{input, {}};
    return Base::stepLoop(input, cb);
  }

  static MatchOutcome<NumGroups> search(InputView<Dir> input) {
    struct Cb {
      InputView<Dir> input;
      MatchOutcome<NumGroups> bestMatch;
      bool foundMatch = false;
      int firstMatchJ = -1;
      int firstAdvanceJ = -1;

      std::size_t startPosition(InputView<Dir> in) { return in.startPos(); }
      Thread seedThread() { return Thread{}; }
      Thread positionThread(std::size_t i) {
        firstMatchJ = -1;
        firstAdvanceJ = -1;
        Thread t;
        t.state = Prog.start_state;
        if constexpr (TrackCaptures) {
          t.groups[0].offset = i;
        }
        return t;
      }
      StepAction onMatch(const Thread& t, std::size_t i, int j) {
        if (firstMatchJ < 0) {
          firstMatchJ = j;
        }
        if constexpr (TrackCaptures) {
          std::size_t startPos = t.groups[0].offset;
          std::size_t matchEnd;
          std::size_t matchStart;
          if constexpr (Dir == Direction::Forward) {
            matchStart = startPos;
            matchEnd = i;
          } else {
            matchStart = i;
            matchEnd = startPos;
          }
          // Extend with trailing dot-star.
          if constexpr (Ast.trailing_dot_star_min >= 0) {
            if constexpr (Dir == Direction::Forward) {
              auto extended = computeDotStarExtension<Direction::Forward>(
                  input,
                  matchEnd,
                  Ast.trailing_dot_star_dot_all,
                  Ast.trailing_dot_star_anchor);
              if (extended == std::string_view::npos) {
                return StepAction::Continue;
              }
              matchEnd = extended;
            } else {
              auto extended = computeDotStarExtension<Direction::Reverse>(
                  input,
                  matchStart,
                  Ast.trailing_dot_star_dot_all,
                  Ast.trailing_dot_star_anchor);
              if (extended == std::string_view::npos) {
                return StepAction::Continue;
              }
              matchStart = extended;
            }
          }
          // Extend with leading dot-star.
          if constexpr (Ast.leading_dot_star_min >= 0) {
            if constexpr (Dir == Direction::Forward) {
              auto extended =
                  computeLeadingDotStarExtension<Direction::Forward>(
                      input,
                      matchStart,
                      Ast.leading_dot_star_dot_all,
                      Ast.leading_dot_star_anchor);
              if (extended == std::string_view::npos) {
                return StepAction::Continue;
              }
              matchStart = extended;
            } else {
              auto extended =
                  computeLeadingDotStarExtension<Direction::Reverse>(
                      input,
                      matchEnd,
                      Ast.leading_dot_star_dot_all,
                      Ast.leading_dot_star_anchor);
              if (extended == std::string_view::npos) {
                return StepAction::Continue;
              }
              matchEnd = extended;
            }
          }
          std::size_t matchLen = matchEnd - matchStart;
          if (!foundMatch || matchStart < bestMatch.state.groups[0].offset ||
              (matchStart == bestMatch.state.groups[0].offset &&
               matchLen > bestMatch.state.groups[0].length)) {
            bestMatch.state.groups = t.groups;
            bestMatch.state.groups[0] = {matchStart, matchLen};
          }
        }
        bestMatch.status = MatchStatus::Matched;
        foundMatch = true;
        return StepAction::Continue;
      }
      void onAdvance(int j) {
        if (firstAdvanceJ < 0) {
          firstAdvanceJ = j;
        }
      }
      bool shouldTerminate(std::size_t, InputView<Dir>, const List& nextList) {
        if (foundMatch && firstMatchJ >= 0 &&
            (firstAdvanceJ < 0 || firstMatchJ < firstAdvanceJ)) {
          if constexpr (TrackCaptures) {
            if (nextList.count == 0 ||
                bestMatch.state.groups[0].offset <=
                    nextList.head->thread.groups[0].offset) {
              return true;
            }
          } else {
            return true;
          }
        }
        if (foundMatch && nextList.count == 0) {
          return true;
        }
        return false;
      }
      MatchOutcome<NumGroups> result() { return bestMatch; }
    };
    Cb cb{input, {}};
    return Base::stepLoop(input, cb);
  }
};

// Lightweight single-pass NFA searcher for position finding.
// Uses minimal per-thread state (state index + start position only).
// No capture group tracking — dramatically faster than full NfaRunner.
struct NfaSearchPosition {
  bool found = false;
  std::size_t start = 0;
  std::size_t end = 0;
};

template <
    const auto& Prog,
    const auto& Ast,
    Flags F,
    Direction Dir = Direction::Forward,
    const auto& ForwardAst = Ast,
    const auto& LookaroundProbeStore = ForwardAst>
struct NfaPositionSearcher
    : NfaExecutorBase<
          NfaPositionSearcherPolicy<Prog>,
          Prog,
          Ast,
          F,
          Dir,
          ForwardAst,
          LookaroundProbeStore> {
  using Base = NfaExecutorBase<
      NfaPositionSearcherPolicy<Prog>,
      Prog,
      Ast,
      F,
      Dir,
      ForwardAst,
      LookaroundProbeStore>;
  using typename Base::List;
  using typename Base::Thread;
  using StepAction = typename Base::StepAction;

  static NfaSearchPosition findFirst(InputView<Dir> input) {
    struct Cb {
      InputView<Dir> input;
      NfaSearchPosition best;
      bool foundMatch = false;
      int firstMatchJ = -1;
      int firstAdvanceJ = -1;

      std::size_t startPosition(InputView<Dir> in) { return in.startPos(); }
      Thread seedThread() { return Thread{}; }
      Thread positionThread(std::size_t i) {
        firstMatchJ = -1;
        firstAdvanceJ = -1;
        if (foundMatch) {
          return Thread{};
        }
        Thread t;
        t.state = Prog.start_state;
        t.startPos = i;
        return t;
      }
      StepAction onMatch(const Thread& t, std::size_t i, int j) {
        if (firstMatchJ < 0) {
          firstMatchJ = j;
        }
        std::size_t matchStart;
        std::size_t matchEnd;
        if constexpr (Dir == Direction::Forward) {
          matchStart = t.startPos;
          matchEnd = i;
        } else {
          matchStart = i;
          matchEnd = t.startPos;
        }
        // Extend with trailing dot-star if active.
        if constexpr (Ast.trailing_dot_star_min >= 0) {
          if constexpr (Dir == Direction::Forward) {
            auto extended = computeDotStarExtension<Direction::Forward>(
                input,
                matchEnd,
                Ast.trailing_dot_star_dot_all,
                Ast.trailing_dot_star_anchor);
            if (extended == std::string_view::npos) {
              return StepAction::Continue;
            }
            matchEnd = extended;
          } else {
            auto extended = computeDotStarExtension<Direction::Reverse>(
                input,
                matchStart,
                Ast.trailing_dot_star_dot_all,
                Ast.trailing_dot_star_anchor);
            if (extended == std::string_view::npos) {
              return StepAction::Continue;
            }
            matchStart = extended;
          }
        }
        // Extend with leading dot-star if active.
        if constexpr (Ast.leading_dot_star_min >= 0) {
          if constexpr (Dir == Direction::Forward) {
            auto extended = computeLeadingDotStarExtension<Direction::Forward>(
                input,
                matchStart,
                Ast.leading_dot_star_dot_all,
                Ast.leading_dot_star_anchor);
            if (extended == std::string_view::npos) {
              return StepAction::Continue;
            }
            matchStart = extended;
          } else {
            auto extended = computeLeadingDotStarExtension<Direction::Reverse>(
                input,
                matchEnd,
                Ast.leading_dot_star_dot_all,
                Ast.leading_dot_star_anchor);
            if (extended == std::string_view::npos) {
              return StepAction::Continue;
            }
            matchEnd = extended;
          }
        }
        std::size_t matchLen = matchEnd - matchStart;
        if (!foundMatch || matchStart < best.start ||
            (matchStart == best.start && matchLen > (best.end - best.start))) {
          best.found = true;
          best.start = matchStart;
          best.end = matchEnd;
        }
        foundMatch = true;
        return StepAction::Continue;
      }
      void onAdvance(int j) {
        if (firstAdvanceJ < 0) {
          firstAdvanceJ = j;
        }
      }
      bool shouldTerminate(std::size_t, InputView<Dir>, const List& nextList) {
        if (foundMatch && firstMatchJ >= 0 &&
            (firstAdvanceJ < 0 || firstMatchJ < firstAdvanceJ)) {
          if (nextList.count == 0 ||
              best.start <= nextList.head->thread.startPos) {
            return true;
          }
        }
        if (foundMatch && nextList.count == 0) {
          return true;
        }
        return false;
      }
      NfaSearchPosition result() { return best; }
    };
    Cb cb{input, {}};
    return Base::stepLoop(input, cb);
  }

  static bool testMatch(InputView<Dir> input) {
    struct Cb {
      bool matched = false;

      std::size_t startPosition(InputView<Dir> in) { return in.startPos(); }
      Thread seedThread() { return Thread{}; }
      Thread positionThread(std::size_t i) {
        Thread t;
        t.state = Prog.start_state;
        t.startPos = i;
        return t;
      }
      StepAction onMatch(const Thread&, std::size_t, int) {
        matched = true;
        return StepAction::Return;
      }
      void onAdvance(int) {}
      bool shouldTerminate(std::size_t, InputView<Dir>, const List&) {
        return false;
      }
      bool result() { return matched; }
    };
    Cb cb{};
    return Base::stepLoop(input, cb);
  }
};

} // namespace folly::regex::detail
