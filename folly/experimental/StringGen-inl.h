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
#error This file may only be included from folly/experimental/StringGen.h
#endif

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
            constexpr size_t kDefaultLineSize = 256;
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


}  // namespace detail
}  // namespace gen
}  // namespace folly

