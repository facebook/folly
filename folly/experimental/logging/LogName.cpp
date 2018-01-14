/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <folly/experimental/logging/LogName.h>

namespace folly {

std::string LogName::canonicalize(StringPiece input) {
  std::string cname;
  cname.reserve(input.size());

  // Ignore trailing '.'s
  size_t end = input.size();
  while (end > 0 && input[end - 1] == '.') {
    --end;
  }

  bool ignoreDot = true;
  for (size_t idx = 0; idx < end; ++idx) {
    if (ignoreDot && input[idx] == '.') {
      continue;
    }
    cname.push_back(input[idx]);
    ignoreDot = (input[idx] == '.');
  }
  return cname;
}

size_t LogName::hash(StringPiece name) {
  // Code based on StringPiece::hash(), but which ignores leading and
  // trailing '.' characters, as well as multiple consecutive '.' characters,
  // so equivalent names result in the same hash.
  uint32_t hash = 5381;

  size_t end = name.size();
  while (end > 0 && name[end - 1] == '.') {
    --end;
  }

  bool ignoreDot = true;
  for (size_t idx = 0; idx < end; ++idx) {
    if (ignoreDot && name[idx] == '.') {
      continue;
    }
    hash = ((hash << 5) + hash) + name[idx];
    // If this character was a '.', ignore subsequent consecutive '.'s
    ignoreDot = (name[idx] == '.');
  }
  return hash;
}

int LogName::cmp(StringPiece a, StringPiece b) {
  // Ignore trailing '.'s
  auto stripTrailingDots = [](StringPiece& s) {
    while (!s.empty() && s.back() == '.') {
      s.uncheckedSubtract(1);
    }
  };
  stripTrailingDots(a);
  stripTrailingDots(b);

  // Advance ptr until it no longer points to a '.'
  // This is used to skip over consecutive sequences of '.' characters.
  auto skipOverDots = [](StringPiece& s) {
    while (!s.empty() && s.front() == '.') {
      s.uncheckedAdvance(1);
    }
  };

  bool ignoreDot = true;
  while (true) {
    if (ignoreDot) {
      skipOverDots(a);
      skipOverDots(b);
    }
    if (a.empty()) {
      return b.empty() ? 0 : -1;
    } else if (b.empty()) {
      return 1;
    }
    if (a.front() != b.front()) {
      return a.front() - b.front();
    }
    ignoreDot = (a.front() == '.');
    a.uncheckedAdvance(1);
    b.uncheckedAdvance(1);
  }
}

StringPiece LogName::getParent(StringPiece name) {
  if (name.empty()) {
    return name;
  }

  ssize_t idx = name.size();

  // Skip over any trailing '.' characters
  while (idx > 0 && name[idx - 1] == '.') {
    --idx;
  }

  // Now walk backwards to the next '.' character
  while (idx > 0 && name[idx - 1] != '.') {
    --idx;
  }

  // And again skip over any '.' characters, in case there are multiple
  // repeated characters.
  while (idx > 0 && name[idx - 1] == '.') {
    --idx;
  }

  return StringPiece(name.begin(), idx);
}
} // namespace folly
