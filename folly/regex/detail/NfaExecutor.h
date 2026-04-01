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
#include <string_view>

#include <folly/regex/detail/Ast.h>
#include <folly/regex/detail/CharClass.h>
#include <folly/regex/detail/Executor.h>
#include <folly/regex/detail/Nfa.h>

namespace folly {
namespace regex {

enum class Flags : unsigned;

namespace detail {

template <int NumGroups, int MaxCounters>
struct NfaThread {
  int state = -1;
  std::array<GroupSpan, NumGroups + 1> groups = {};
  int counters[MaxCounters] = {};
};

template <>
struct NfaThread<-1, 0> {
  int state = -1;
};

template <int NumGroups>
struct NfaThread<NumGroups, 0> {
  int state = -1;
  std::array<GroupSpan, NumGroups + 1> groups = {};
};

template <int MaxCounters>
struct NfaThread<-1, MaxCounters> {
  int state = -1;
  int counters[MaxCounters] = {};
};

template <
    const auto& Prog,
    const auto& Ast,
    Flags F,
    bool TrackCaptures,
    int NumGroups>
struct NfaRunner {
  static constexpr int kMaxStates = Prog.state_count;
  static constexpr int kMaxCounters = Prog.num_counters;
  static constexpr int kMaxRepeatValue = Prog.max_repeat_value;
  using Thread = NfaThread<TrackCaptures ? NumGroups : -1, kMaxCounters>;

  struct ThreadList {
    Thread threads[kMaxStates > 0 ? kMaxStates * 2 : 1] = {};
    int count = 0;
    FixedBitset<(kMaxStates > 0 ? kMaxStates : 1)> inList = {};

    // Per-counter dedup bitset: counterSeen[counter_id] has bit i set if a
    // thread with counter_value == i has already been added this position.
    FixedBitset<(kMaxRepeatValue > 0 ? kMaxRepeatValue + 1 : 1)>
        counterSeen[kMaxCounters > 0 ? kMaxCounters : 1] = {};

    void clear() {
      count = 0;
      inList.clearAll();
      if constexpr (kMaxCounters > 0) {
        for (int i = 0; i < kMaxCounters; ++i) {
          counterSeen[i].clearAll();
        }
      }
    }

    void add(Thread t) {
      if (t.state >= 0 && t.state < kMaxStates && !inList.test(t.state)) {
        inList.set(t.state);
        threads[count++] = t;
      }
    }

    void addCounted(Thread t, int counterId, int counterValue) {
      if constexpr (kMaxCounters > 0) {
        if (counterSeen[counterId].test(counterValue)) {
          return;
        }
        counterSeen[counterId].set(counterValue);
        threads[count++] = t;
      }
    }
  };

  static void addState(
      ThreadList& list, Thread t, std::string_view input, std::size_t pos) {
    if (t.state < 0 || t.state >= Prog.state_count) {
      return;
    }
    if (list.inList.test(t.state)) {
      return;
    }

    const auto& s = Prog.states[t.state];

    switch (s.kind) {
      case NfaStateKind::Split: {
        Thread t1 = t;
        t1.state = s.next;
        addState(list, t1, input, pos);
        Thread t2 = t;
        t2.state = s.alt;
        addState(list, t2, input, pos);
        return;
      }
      case NfaStateKind::CountedRepeat: {
        if constexpr (kMaxCounters > 0) {
          int cv = t.counters[s.counter_id];
          if (cv < s.min_repeat) {
            // Must loop — haven't reached minimum yet
            Thread tLoop = t;
            tLoop.counters[s.counter_id] = cv + 1;
            tLoop.state = s.alt;
            list.addCounted(tLoop, s.counter_id, cv + 1);
            addState(list, tLoop, input, pos);
          } else if (s.max_repeat >= 0 && cv >= s.max_repeat) {
            // Must exit — reached maximum
            Thread tExit = t;
            tExit.counters[s.counter_id] = 0;
            tExit.state = s.next;
            addState(list, tExit, input, pos);
          } else {
            // Can loop or exit — greedy decides priority
            if (s.greedy) {
              Thread tLoop = t;
              tLoop.counters[s.counter_id] = cv + 1;
              tLoop.state = s.alt;
              list.addCounted(tLoop, s.counter_id, cv + 1);
              addState(list, tLoop, input, pos);
              Thread tExit = t;
              tExit.counters[s.counter_id] = 0;
              tExit.state = s.next;
              addState(list, tExit, input, pos);
            } else {
              Thread tExit = t;
              tExit.counters[s.counter_id] = 0;
              tExit.state = s.next;
              addState(list, tExit, input, pos);
              Thread tLoop = t;
              tLoop.counters[s.counter_id] = cv + 1;
              tLoop.state = s.alt;
              list.addCounted(tLoop, s.counter_id, cv + 1);
              addState(list, tLoop, input, pos);
            }
          }
        }
        return;
      }
      case NfaStateKind::GroupStart: {
        if constexpr (TrackCaptures) {
          t.groups[s.group_id].offset = pos;
          t.groups[s.group_id].length = 0;
        }
        t.state = s.next;
        addState(list, t, input, pos);
        return;
      }
      case NfaStateKind::GroupEnd: {
        if constexpr (TrackCaptures) {
          t.groups[s.group_id].length = pos - t.groups[s.group_id].offset;
        }
        t.state = s.next;
        addState(list, t, input, pos);
        return;
      }
      case NfaStateKind::AnchorBegin: {
        if (pos == 0) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEnd: {
        if (pos == input.size()) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorStartOfString: {
        if (pos == 0) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEndOfString: {
        if (pos == input.size()) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEndOfStringOrNewline: {
        if (pos == input.size() ||
            (pos + 1 == input.size() && input[pos] == '\n')) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorBeginLine: {
        if (pos == 0 || input[pos - 1] == '\n') {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEndLine: {
        if (pos == input.size() || input[pos] == '\n') {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::Match:
      case NfaStateKind::Literal:
      case NfaStateKind::AnyChar:
      case NfaStateKind::AnyByte:
      case NfaStateKind::CharClass:
        list.add(t);
        return;
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
      case NfaStateKind::AnyChar:
        return true;
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
        return false;
    }
  }

  static MatchOutcome<NumGroups> matchAnchored(std::string_view input) {
    ThreadList current, next;
    current.clear();
    next.clear();

    Thread startThread;
    startThread.state = Prog.start_state;
    addState(current, startThread, input, 0);

    MatchOutcome<NumGroups> bestMatch;

    for (std::size_t i = 0; i <= input.size(); ++i) {
      for (int j = 0; j < current.count; ++j) {
        const auto& t = current.threads[j];
        const auto& s = Prog.states[t.state];

        if (s.kind == NfaStateKind::Match) {
          if (i == input.size()) {
            bestMatch.status = MatchStatus::Matched;
            if constexpr (TrackCaptures) {
              bestMatch.state.groups = t.groups;
              bestMatch.state.groups[0] = {0, input.size()};
            }
            return bestMatch;
          }
          continue;
        }

        if (i < input.size() && stateMatches(s, input[i])) {
          Thread nt = t;
          nt.state = s.next;
          addState(next, nt, input, i + 1);
        }
      }

      current.clear();
      auto tmp = current;
      current = next;
      next = tmp;
      next.clear();
    }

    return bestMatch;
  }

  static MatchOutcome<NumGroups> search(std::string_view input) {
    ThreadList current, next;
    current.clear();
    next.clear();

    MatchOutcome<NumGroups> bestMatch;
    bool foundMatch = false;

    for (std::size_t i = 0; i <= input.size(); ++i) {
      // Add start state at every position for unanchored search
      Thread startThread;
      startThread.state = Prog.start_state;
      if constexpr (TrackCaptures) {
        startThread.groups[0].offset = i;
      }
      addState(current, startThread, input, i);

      for (int j = 0; j < current.count; ++j) {
        const auto& t = current.threads[j];
        const auto& s = Prog.states[t.state];

        if (s.kind == NfaStateKind::Match) {
          // Update best match — always prefer longer match at same start
          bestMatch.status = MatchStatus::Matched;
          if constexpr (TrackCaptures) {
            std::size_t startPos = t.groups[0].offset;
            std::size_t matchLen = i - startPos;
            if (!foundMatch || matchLen > bestMatch.state.groups[0].length) {
              bestMatch.state.groups = t.groups;
              bestMatch.state.groups[0].length = matchLen;
            }
          }
          foundMatch = true;
          continue;
        }

        if (i < input.size() && stateMatches(s, input[i])) {
          Thread nt = t;
          nt.state = s.next;
          addState(next, nt, input, i + 1);
        }
      }

      if (foundMatch && next.count == 0) {
        return bestMatch;
      }

      current.clear();
      auto tmp = current;
      current = next;
      next = tmp;
      next.clear();
    }

    if (foundMatch) {
      return bestMatch;
    }

    return bestMatch;
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

template <const auto& Prog, const auto& Ast, Flags F>
struct NfaPositionSearcher {
  static constexpr int kMaxStates = Prog.state_count;

  struct PosThread {
    int state = -1;
    std::size_t startPos = 0;
  };

  struct PosThreadList {
    PosThread threads[kMaxStates > 0 ? kMaxStates * 2 : 1] = {};
    int count = 0;
    FixedBitset<(kMaxStates > 0 ? kMaxStates : 1)> inList = {};

    void clear() {
      count = 0;
      inList.clearAll();
    }

    void add(PosThread t) {
      if (t.state >= 0 && t.state < kMaxStates && !inList.test(t.state)) {
        inList.set(t.state);
        threads[count++] = t;
      }
    }
  };

  static void addState(
      PosThreadList& list,
      PosThread t,
      std::string_view input,
      std::size_t pos) {
    if (t.state < 0 || t.state >= Prog.state_count) {
      return;
    }
    if (list.inList.test(t.state)) {
      return;
    }

    const auto& s = Prog.states[t.state];

    switch (s.kind) {
      case NfaStateKind::Split: {
        PosThread t1 = t;
        t1.state = s.next;
        addState(list, t1, input, pos);
        PosThread t2 = t;
        t2.state = s.alt;
        addState(list, t2, input, pos);
        return;
      }
      case NfaStateKind::CountedRepeat: {
        // Position searcher doesn't track counts — conservatively
        // follow both paths (loop and exit) to find all possible
        // match positions. The full NFA runner verifies exact counts.
        PosThread tLoop = t;
        tLoop.state = s.alt;
        addState(list, tLoop, input, pos);
        PosThread tExit = t;
        tExit.state = s.next;
        addState(list, tExit, input, pos);
        return;
      }
      case NfaStateKind::GroupStart:
      case NfaStateKind::GroupEnd: {
        t.state = s.next;
        addState(list, t, input, pos);
        return;
      }
      case NfaStateKind::AnchorBegin: {
        if (pos == 0) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEnd: {
        if (pos == input.size()) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorStartOfString: {
        if (pos == 0) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEndOfString: {
        if (pos == input.size()) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEndOfStringOrNewline: {
        if (pos == input.size() ||
            (pos + 1 == input.size() && input[pos] == '\n')) {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorBeginLine: {
        if (pos == 0 || input[pos - 1] == '\n') {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::AnchorEndLine: {
        if (pos == input.size() || input[pos] == '\n') {
          t.state = s.next;
          addState(list, t, input, pos);
        }
        return;
      }
      case NfaStateKind::Match:
      case NfaStateKind::Literal:
      case NfaStateKind::AnyChar:
      case NfaStateKind::AnyByte:
      case NfaStateKind::CharClass:
        list.add(t);
        return;
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
      case NfaStateKind::AnyChar:
        return true;
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
        return false;
    }
  }

  static NfaSearchPosition findFirst(std::string_view input) {
    PosThreadList current, next;
    current.clear();
    next.clear();

    NfaSearchPosition best;
    bool foundMatch = false;

    for (std::size_t i = 0; i <= input.size(); ++i) {
      if (!foundMatch) {
        PosThread startThread;
        startThread.state = Prog.start_state;
        startThread.startPos = i;
        addState(current, startThread, input, i);
      }

      for (int j = 0; j < current.count; ++j) {
        const auto& t = current.threads[j];
        const auto& s = Prog.states[t.state];

        if (s.kind == NfaStateKind::Match) {
          std::size_t matchLen = i - t.startPos;
          if (!foundMatch || matchLen > (best.end - best.start)) {
            best.found = true;
            best.start = t.startPos;
            best.end = i;
          }
          foundMatch = true;
          continue;
        }

        if (i < input.size() && stateMatches(s, input[i])) {
          PosThread nt = t;
          nt.state = s.next;
          addState(next, nt, input, i + 1);
        }
      }

      if (foundMatch && next.count == 0) {
        return best;
      }

      current.clear();
      auto tmp = current;
      current = next;
      next = tmp;
      next.clear();
    }

    return best;
  }

  static bool testMatch(std::string_view input) {
    PosThreadList current, next;
    current.clear();
    next.clear();

    for (std::size_t i = 0; i <= input.size(); ++i) {
      PosThread startThread;
      startThread.state = Prog.start_state;
      startThread.startPos = i;
      addState(current, startThread, input, i);

      for (int j = 0; j < current.count; ++j) {
        const auto& t = current.threads[j];
        const auto& s = Prog.states[t.state];

        if (s.kind == NfaStateKind::Match) {
          return true;
        }

        if (i < input.size() && stateMatches(s, input[i])) {
          PosThread nt = t;
          nt.state = s.next;
          addState(next, nt, input, i + 1);
        }
      }

      current.clear();
      auto tmp = current;
      current = next;
      next = tmp;
      next.clear();
    }

    return false;
  }
};

} // namespace detail
} // namespace regex
} // namespace folly
