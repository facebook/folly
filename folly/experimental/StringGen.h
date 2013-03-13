/*
 * Copyright 2013 Facebook, Inc.
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

template<class Delimiter, class Output>
class Unsplit;

template<class Delimiter, class OutputBuffer>
class UnsplitBuffer;
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

/*
 * Joins a sequence of tokens into a string, with the chosen delimiter.
 *
 * E.G.
 *   fbstring result = split("a,b,c", ",") | unsplit(",");
 *   assert(result == "a,b,c");
 *
 *   std::string result = split("a,b,c", ",") | unsplit<std::string>(" ");
 *   assert(result == "a b c");
 */


// NOTE: The template arguments are reversed to allow the user to cleanly
// specify the output type while still inferring the type of the delimiter.
template<class Output = folly::fbstring,
         class Delimiter,
         class Unsplit = detail::Unsplit<Delimiter, Output>>
Unsplit unsplit(const Delimiter& delimiter) {
  return Unsplit(delimiter);
}

template<class Output = folly::fbstring,
         class Unsplit = detail::Unsplit<fbstring, Output>>
Unsplit unsplit(const char* delimiter) {
  return Unsplit(delimiter);
}

/*
 * Joins a sequence of tokens into a string, appending them to the output
 * buffer.  If the output buffer is empty, an initial delimiter will not be
 * inserted at the start.
 *
 * E.G.
 *   std::string buffer;
 *   split("a,b,c", ",") | unsplit(",", &buffer);
 *   assert(buffer == "a,b,c");
 *
 *   std::string anotherBuffer("initial");
 *   split("a,b,c", ",") | unsplit(",", &anotherbuffer);
 *   assert(anotherBuffer == "initial,a,b,c");
 */
template<class Delimiter,
         class OutputBuffer,
         class UnsplitBuffer = detail::UnsplitBuffer<Delimiter, OutputBuffer>>
UnsplitBuffer unsplit(const Delimiter& delimiter, OutputBuffer* outputBuffer) {
  return UnsplitBuffer(delimiter, outputBuffer);
}

template<class OutputBuffer,
         class UnsplitBuffer = detail::UnsplitBuffer<fbstring, OutputBuffer>>
UnsplitBuffer unsplit(const char* delimiter, OutputBuffer* outputBuffer) {
  return UnsplitBuffer(delimiter, outputBuffer);
}

}  // namespace gen
}  // namespace folly

#include "folly/experimental/StringGen-inl.h"

#endif /* FOLLY_STRINGGEN_H_ */

