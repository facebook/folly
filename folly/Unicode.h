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

// Some utility routines relating to unicode.

#ifndef FOLLY_UNICODE_H_
#define FOLLY_UNICODE_H_

#include "folly/FBString.h"

namespace folly {

//////////////////////////////////////////////////////////////////////

/*
 * Encode a single unicode code point into a UTF-8 byte sequence.
 *
 * Return value is undefined if `cp' is an invalid code point.
 */
fbstring codePointToUtf8(char32_t cp);

//////////////////////////////////////////////////////////////////////

}

#endif
