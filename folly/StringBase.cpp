/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/String.h>

namespace folly {

StringPiece skipWhitespace(StringPiece sp) {
  // Spaces other than ' ' characters are less common but should be
  // checked.  This configuration where we loop on the ' '
  // separately from oddspaces was empirically fastest.
  auto oddspace = [] (char c) {
    return c == '\n' || c == '\t' || c == '\r';
  };

loop:
  for (; !sp.empty() && sp.front() == ' '; sp.pop_front()) {
  }
  if (!sp.empty() && oddspace(sp.front())) {
    sp.pop_front();
    goto loop;
  }

  return sp;
}

}
