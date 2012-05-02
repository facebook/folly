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

#ifndef FOLLY_IO_STREAM_H_
#define FOLLY_IO_STREAM_H_

#include <boost/iterator/iterator_facade.hpp>
#include <glog/logging.h>

#include "folly/Range.h"
#include "folly/FBString.h"
#include "folly/experimental/io/IOBuf.h"

namespace folly {

/**
 * An InputByteStream is a functional object with the following signature:
 *
 *   bool operator()(ByteRange& data);
 *
 * Input byte streams must be movable.
 *
 * The stream returns false at EOF; otherwise, it returns true and sets data to
 * the next chunk of data from the stream.  The memory that data points to must
 * remain valid until the next call to the stream.  In case of error, the
 * stream throws an exception.
 *
 * The meaning of a "chunk" is left up to the stream implementation.  Some
 * streams return chunks limited to the size of an internal buffer.  Other
 * streams return the entire input as one (potentially huge) ByteRange.
 * Others assign meaning to chunks: StreamSplitter returns "lines" -- sequences
 * of bytes between delimiters.  This ambiguity is intentional; resolving it
 * would significantly increase the complexity of the code.
 *
 * An OutputByteStream is an object with the following signature:
 *
 *   void operator()(ByteRange data);
 *   void close();
 *
 * Output byte streams must be movable.
 *
 * The stream appends a chunk of data to the stream when calling operator().
 * close() closes the stream, allowing us to detect any errors before
 * destroying the stream object (to avoid throwing exceptions from the
 * destructor).  The destructor must close the stream if close() was not
 * explicitly called, and abort the program if closing the stream caused
 * an error.
 *
 * Just like with input byte streams, the meaning of a "chunk" is left up
 * to the stream implementation.  Some streams will just append all chunks
 * as given; others might assign meaning to chunks and (for example) append
 * delimiters between chunks.
 */

template <class Stream> class InputByteStreamIterator;

/**
 * Convenient base class template to derive all streams from; provides begin()
 * and end() for iterator access.  This class makes use of the curriously
 * recurring template pattern; your stream class S may derive from
 * InputByteStreamBase<S>.
 *
 * Deriving from InputByteStreamBase<S> is not required, but is convenient.
 */
template <class Derived>
class InputByteStreamBase {
 public:
  InputByteStreamIterator<Derived> begin() {
    return InputByteStreamIterator<Derived>(static_cast<Derived&>(*this));
  }

  InputByteStreamIterator<Derived> end() {
    return InputByteStreamIterator<Derived>();
  }

  InputByteStreamBase() { }
  InputByteStreamBase(InputByteStreamBase&&) = default;
  InputByteStreamBase& operator=(InputByteStreamBase&&) = default;

 private:
  InputByteStreamBase(const InputByteStreamBase&) = delete;
  InputByteStreamBase& operator=(const InputByteStreamBase&) = delete;
};

/**
 * Stream iterator
 */
template <class Stream>
class InputByteStreamIterator
  : public boost::iterator_facade<
      InputByteStreamIterator<Stream>,
      const ByteRange,
      boost::single_pass_traversal_tag> {
 public:
  InputByteStreamIterator() : stream_(nullptr) { }

  explicit InputByteStreamIterator(Stream& stream) : stream_(&stream) {
    increment();
  }

 private:
  friend class boost::iterator_core_access;

  void increment() {
    DCHECK(stream_);
    if (stream_ && !(*stream_)(chunk_)) {
      stream_ = nullptr;
    }
  }

  // This is a single pass iterator, so all we care about is that
  // equal forms an equivalence class on the subset of iterators that it's
  // defined on.  In our case, only identical (same object) iterators and
  // past-the-end iterators compare equal.  (so that it != end() works)
  bool equal(const InputByteStreamIterator& other) const {
    return (this == &other) || (!stream_ && !other.stream_);
  }

  const ByteRange& dereference() const {
    DCHECK(stream_);
    return chunk_;
  }

  Stream* stream_;
  ByteRange chunk_;
};

/**
 * Stream that read()s from a file.
 */
class FileInputByteStream : public InputByteStreamBase<FileInputByteStream> {
 public:
  static const size_t kDefaultBufferSize = 4096;
  explicit FileInputByteStream(int fd,
                               bool ownsFd = false,
                               size_t bufferSize = kDefaultBufferSize);
  FileInputByteStream(int fd, bool ownsFd, std::unique_ptr<IOBuf>&& buffer);
  FileInputByteStream(FileInputByteStream&& other);
  FileInputByteStream& operator=(FileInputByteStream&& other);
  ~FileInputByteStream();
  bool operator()(ByteRange& chunk);

 private:
  void closeNoThrow();

  int fd_;
  bool ownsFd_;
  std::unique_ptr<IOBuf> buffer_;
};

/**
 * Split a stream on a delimiter.  Returns "lines" between delimiters;
 * the delimiters are not included in the returned string.
 *
 * Note that the InputByteStreamSplitter acts as a stream itself, and you can
 * iterate over it.
 */
template <class Stream>
class InputByteStreamSplitter
  : public InputByteStreamBase<InputByteStreamSplitter<Stream>> {
 public:
  InputByteStreamSplitter(char delimiter, Stream stream);
  bool operator()(ByteRange& chunk);

  InputByteStreamSplitter(InputByteStreamSplitter&&) = default;
  InputByteStreamSplitter& operator=(InputByteStreamSplitter&&) = default;

 private:
  InputByteStreamSplitter(const InputByteStreamSplitter&) = delete;
  InputByteStreamSplitter& operator=(const InputByteStreamSplitter&) = delete;

  bool done_;
  char delimiter_;
  Stream stream_;
  std::unique_ptr<IOBuf> buffer_;
  ByteRange rest_;
};

/**
 * Shortcut to create a stream splitter around a stream and deduce
 * the type of the template argument.
 */
template <class Stream>
InputByteStreamSplitter<Stream> makeInputByteStreamSplitter(
    char delimiter, Stream stream) {
  return InputByteStreamSplitter<Stream>(delimiter, std::move(stream));
}

/**
 * Create a stream that splits a file into chunks (default: lines, with
 * '\n' as the delimiter)
 */
InputByteStreamSplitter<FileInputByteStream> byLine(
    const char* fileName, char delim='\n');

// overload for std::string
inline InputByteStreamSplitter<FileInputByteStream> byLine(
    const std::string& fileName, char delim='\n') {
  return byLine(fileName.c_str(), delim);
}

// overload for fbstring
inline InputByteStreamSplitter<FileInputByteStream> byLine(
    const fbstring& fileName, char delim='\n') {
  return byLine(fileName.c_str(), delim);
}

}  // namespace folly

#include "folly/experimental/io/Stream-inl.h"

#endif /* FOLLY_IO_STREAM_H_ */

