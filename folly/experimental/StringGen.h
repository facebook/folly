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

#ifndef FOLLY_STRINGGEN_H_
#define FOLLY_STRINGGEN_H_

#include "folly/Range.h"

namespace folly {
namespace gen {

namespace detail {
class StringResplitter;
class SplitStringSource;
}  // namespace detail

/**
 * Split the output from a generator into StringPiece "lines" delimited by
 * the given delimiter.  Delimters are NOT included in the output.
 *
 * resplit() behaves as if the input strings were concatenated into one long
 * string and then split.
 */
// make this a template so we don't require StringResplitter to be complete
// until use
template <class S=detail::StringResplitter>
S resplit(char delimiter) {
  return S(delimiter);
}

template <class S=detail::SplitStringSource>
S split(const StringPiece& source, char delimiter) {
  return S(source, delimiter);
}

}  // namespace gen
}  // namespace folly

#include "folly/experimental/StringGen-inl.h"

#endif /* FOLLY_STRINGGEN_H_ */

