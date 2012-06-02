/*
 * Copyright 2012 Facebook, Inc.
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

//
// @author Mark Rabkin (mrabkin@fb.com)
// @author Andrei Alexandrescu (andrei.alexandrescu@fb.com)
//

#include "folly/Range.h"

namespace folly {

/**
Predicates that can be used with qfind and startsWith
 */
const AsciiCaseSensitive asciiCaseSensitive = AsciiCaseSensitive();
const AsciiCaseInsensitive asciiCaseInsensitive = AsciiCaseInsensitive();

std::ostream& operator<<(std::ostream& os, const StringPiece& piece) {
  os.write(piece.start(), piece.size());
  return os;
}

} // namespace folly
