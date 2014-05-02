/*
 * Copyright 2014 Facebook, Inc.
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

#ifndef FOLLY_GEN_STRING_H
#error This file may only be included from folly/gen/String.h
#endif

#include "folly/Conv.h"
#include "folly/String.h"
#include "folly/io/IOBuf.h"

namespace folly {
namespace gen {
namespace detail {

inline bool splitPrefix(StringPiece& in, StringPiece& prefix, char delimiter) {
  auto p = static_cast<const char*>(memchr(in.data(), delimiter, in.size()));
  if (p) {
    prefix.assign(in.data(), p);
    in.assign(p + 1, in.end());
    return true;
  }
  prefix.clear();
  return false;
}

inline const char* ch(const unsigned char* p) {
  return reinterpret_cast<const char*>(p);
}

class StringResplitter : public Operator<StringResplitter> {
  char delimiter_;
 public:
  explicit StringResplitter(char delimiter) : delimiter_(delimiter) { }

  template <class Source>
  class Generator : public GenImpl<StringPiece, Generator<Source>> {
    Source source_;
    char delimiter_;
   public:
    Generator(Source source, char delimiter)
      : source_(std::move(source)), delimiter_(delimiter) { }

    template <class Body>
    bool apply(Body&& body) const {
      std::unique_ptr<IOBuf> buffer;

      auto fn = [&](StringPiece in) -> bool {
        StringPiece prefix;
        bool found = splitPrefix(in, prefix, this->delimiter_);
        if (found && buffer && buffer->length() != 0) {
          // Append to end of buffer, return line
          if (!prefix.empty()) {
            buffer->reserve(0, prefix.size());
            memcpy(buffer->writableTail(), prefix.data(), prefix.size());
            buffer->append(prefix.size());
          }
          if (!body(StringPiece(ch(buffer->data()), buffer->length()))) {
            return false;
          }
          buffer->clear();
          found = splitPrefix(in, prefix, this->delimiter_);
        }
        // Buffer is empty, return lines directly from input (no buffer)
        while (found) {
          if (!body(prefix)) {
            return false;
          }
          found = splitPrefix(in, prefix, this->delimiter_);
        }
        if (!in.empty()) {
          // Incomplete line left, append to buffer
          if (!buffer) {
            // Arbitrarily assume that we have half a line and get enough
            // room for twice that.
            FOLLY_CONSTEXPR size_t kDefaultLineSize = 256;
            buffer = IOBuf::create(std::max(kDefaultLineSize, 2 * in.size()));
          }
          buffer->reserve(0, in.size());
          memcpy(buffer->writableTail(), in.data(), in.size());
          buffer->append(in.size());
        }
        return true;
      };

      // Iterate
      if (!source_.apply(std::move(fn))) {
        return false;
      }

      // Incomplete last line
      if (buffer && buffer->length() != 0) {
        if (!body(StringPiece(ch(buffer->data()), buffer->length()))) {
          return false;
        }
      }
      return true;
    }

	static FOLLY_CONSTEXPR bool infinite = Source::infinite;
  };

  template<class Source,
           class Value,
           class Gen = Generator<Source>>
  Gen compose(GenImpl<Value, Source>&& source) const {
    return Gen(std::move(source.self()), delimiter_);
  }

  template<class Source,
           class Value,
           class Gen = Generator<Source>>
  Gen compose(const GenImpl<Value, Source>& source) const {
    return Gen(source.self(), delimiter_);
  }
};

class SplitStringSource : public GenImpl<StringPiece, SplitStringSource> {
  StringPiece source_;
  char delimiter_;
 public:
  SplitStringSource(const StringPiece& source,
                    char delimiter)
    : source_(source)
    , delimiter_(delimiter) { }

  template <class Body>
  bool apply(Body&& body) const {
    StringPiece rest(source_);
    StringPiece prefix;
    while (splitPrefix(rest, prefix, this->delimiter_)) {
      if (!body(prefix)) {
        return false;
      }
    }
    if (!rest.empty()) {
      if (!body(rest)) {
        return false;
      }
    }
    return true;
  }
};

/**
 * Unsplit - For joining tokens from a generator into a string.  This is
 * the inverse of `split` above.
 *
 * This type is primarily used through the 'unsplit' function.
 */
template<class Delimiter,
         class Output>
class Unsplit : public Operator<Unsplit<Delimiter, Output>> {
  Delimiter delimiter_;
 public:
  Unsplit(const Delimiter& delimiter)
    : delimiter_(delimiter) {
  }

  template<class Source,
           class Value>
  Output compose(const GenImpl<Value, Source>& source) const {
    Output outputBuffer;
    UnsplitBuffer<Delimiter, Output> unsplitter(delimiter_, &outputBuffer);
    unsplitter.compose(source);
    return outputBuffer;
  }
};

/**
 * UnsplitBuffer - For joining tokens from a generator into a string,
 * and inserting them into a custom buffer.
 *
 * This type is primarily used through the 'unsplit' function.
 */
template<class Delimiter,
         class OutputBuffer>
class UnsplitBuffer : public Operator<UnsplitBuffer<Delimiter, OutputBuffer>> {
  Delimiter delimiter_;
  OutputBuffer* outputBuffer_;
 public:
  UnsplitBuffer(const Delimiter& delimiter, OutputBuffer* outputBuffer)
    : delimiter_(delimiter)
    , outputBuffer_(outputBuffer) {
    CHECK(outputBuffer);
  }

  template<class Source,
           class Value>
  void compose(const GenImpl<Value, Source>& source) const {
    // If the output buffer is empty, we skip inserting the delimiter for the
    // first element.
    bool skipDelim = outputBuffer_->empty();
    source | [&](Value v) {
      if (skipDelim) {
        skipDelim = false;
        toAppend(std::forward<Value>(v), outputBuffer_);
      } else {
        toAppend(delimiter_, std::forward<Value>(v), outputBuffer_);
      }
    };
  }
};


/**
 * Hack for static for-like constructs
 */
template<class Target, class=void>
inline Target passthrough(Target target) { return target; }

#pragma GCC diagnostic push
#ifdef __clang__
// Clang isn't happy with eatField() hack below.
#pragma GCC diagnostic ignored "-Wreturn-stack-address"
#endif  // __clang__

/**
 * ParseToTuple - For splitting a record and immediatlely converting it to a
 * target tuple type. Primary used through the 'eachToTuple' helper, like so:
 *
 *  auto config
 *    = split("1:a 2:b", ' ')
 *    | eachToTuple<int, string>()
 *    | as<vector<tuple<int, string>>>();
 *
 */
template<class TargetContainer,
         class Delimiter,
         class... Targets>
class SplitTo {
  Delimiter delimiter_;
 public:
  explicit SplitTo(Delimiter delimiter)
    : delimiter_(delimiter) {}

  TargetContainer operator()(StringPiece line) const {
    int i = 0;
    StringPiece fields[sizeof...(Targets)];
    // HACK(tjackson): Used for referencing fields[] corresponding to variadic
    // template parameters.
    auto eatField = [&]() -> StringPiece& { return fields[i++]; };
    if (!split(delimiter_,
               line,
               detail::passthrough<StringPiece&, Targets>(eatField())...)) {
      throw std::runtime_error("field count mismatch");
    }
    i = 0;
    return TargetContainer(To<Targets>()(eatField())...);
  }
};

#pragma GCC diagnostic pop

}  // namespace detail

}  // namespace gen
}  // namespace folly
