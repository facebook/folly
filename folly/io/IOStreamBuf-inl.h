/*
 * Copyright 2017 Dish Network.
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

#pragma once

#include <folly/Likely.h>

namespace folly {

template <typename CharT, typename Traits>
IOStreamBuf<CharT,Traits>::IOStreamBuf(folly::IOBuf const* head)
  : std::basic_streambuf<CharT,Traits>(),
    head_(head),
    gcur_(head) {
  csetg(gcur_->data(), gcur_->data(), gcur_->tail());
}

template <typename CharT, typename Traits>
void IOStreamBuf<CharT,Traits>::swap(IOStreamBuf<CharT,Traits>& rhs) {
  std::basic_streambuf<CharT,Traits>::swap(rhs);
  std::swap(head_, rhs.head_);
  std::swap(gcur_, rhs.gcur_);
}

template <typename CharT, typename Traits>
typename IOStreamBuf<CharT,Traits>::pos_type const
IOStreamBuf<CharT,Traits>::badoff =
  static_cast<typename IOStreamBuf<CharT,Traits>::pos_type>(
    static_cast<typename IOStreamBuf<CharT,Traits>::off_type>(-1));

// This is called either to rewind the get area (because gptr() == eback())
// or to attempt to put back a non-matching character (which we disallow
// on non-mutable IOBufs).
template <typename CharT, typename Traits>
typename IOStreamBuf<CharT,Traits>::int_type
IOStreamBuf<CharT,Traits>::pbackfail(int_type c) {
  if (this->gptr() != this->eback()) {
    return traits_type::eof(); // trying to putback non-matching character
  }

  if (gcur_ == head_) {
    // Already at beginning of first IOBuf
    return traits_type::eof();
  }

  // Find the next preceding non-empty IOBuf, back to head_
  // Ensure the object state is not modified unless an earlier input sequence
  // can be found.
  IOBuf const* prev = gcur_;
  do {
    prev = prev->prev();
  } while (prev->length() == 0 && prev != head_);

  // Check whether c matches potential *gptr() before updating pointers
  if (!Traits::eq(c, Traits::to_int_type(prev->tail()[-1]))) {
    return traits_type::eof();
  }

  gcur_ = prev;

  csetg(gcur_->data(), gcur_->tail() - 1, gcur_->tail());

  return traits_type::to_int_type(*this->gptr());
}

template <typename CharT, typename Traits>
typename IOStreamBuf<CharT,Traits>::int_type
IOStreamBuf<CharT,Traits>::underflow() {
  // public methods only call underflow() when gptr() >= egptr()
  // (but it's not an error to call underflow when gptr() < egptr())
  if (UNLIKELY(this->gptr() < this->egptr())) {
    return traits_type::to_int_type(*this->gptr());
  }

  // Also handles non-chained
  IOBuf const* next = gcur_->next();
  if (next == head_) {
    return traits_type::eof();
  }

  gcur_ = next;
  csetg(gcur_->data(), gcur_->data(), gcur_->tail());

  return traits_type::to_int_type(*this->gptr());
}

template <typename CharT, typename Traits>
typename IOStreamBuf<CharT,Traits>::pos_type
IOStreamBuf<CharT,Traits>::current_position() const {
  pos_type pos = 0;

  for (IOBuf const* buf = head_; buf != gcur_; buf = buf->next()) {
    pos += buf->length();
  }

  return pos + (this->gptr() - this->eback());
}

template <typename CharT, typename Traits>
typename IOStreamBuf<CharT,Traits>::pos_type
IOStreamBuf<CharT,Traits>::seekoff(off_type off,
                                   std::ios_base::seekdir way,
                                   std::ios_base::openmode which) {
  if ((which & std::ios_base::in) != std::ios_base::in) {
    return badoff;
  }

  if (way == std::ios_base::beg) {
    if (UNLIKELY(off < 0)) {
      return badoff;
    }

    IOBuf const* buf = head_;

    size_t remaining_offset = static_cast<size_t>(off);

    while (remaining_offset > buf->length()) {
      remaining_offset -= buf->length();
      buf = buf->next();
      if (buf == head_) {
        return badoff;
      }
    }

    gcur_ = buf;
    csetg(gcur_->data(), gcur_->data() + remaining_offset, gcur_->tail());

    return pos_type(off);
  }

  if (way == std::ios_base::end) {
    if (UNLIKELY(off > 0)) {
      return badoff;
    }

    IOBuf const* buf = head_->prev();

    // Work with positive offset working back from the last tail()
    size_t remaining_offset = static_cast<size_t>(0 - off);

    while (remaining_offset > buf->length()) {
      remaining_offset -= buf->length();
      buf = buf->prev();
      if (buf == head_ && remaining_offset > buf->length()) {
        return badoff;
      }
    }

    gcur_ = buf;
    csetg(gcur_->data(), gcur_->tail() - remaining_offset, gcur_->tail());

    return current_position();
  }

  if (way == std::ios_base::cur) {
    if (off == 0) { // commonly called by tellg()
      return current_position();
    }

    IOBuf const* buf = gcur_;

    if (off < 0) {
      // backwards; use as positive distance backward
      size_t remaining_offset = static_cast<size_t>(0 - off);

      if (remaining_offset <
              static_cast<size_t>(this->gptr() - this->eback())) {
        // In the same IOBuf
        csetg(gcur_->data(),
              gcur_->data() +
                static_cast<size_t>(this->gptr() - this->eback()) -
                remaining_offset,
              gcur_->tail());
        return current_position();
      }

      remaining_offset -= this->gptr() - this->eback();
      buf = buf->prev();

      while (remaining_offset > buf->length()) {
        if (buf == head_) {
          return badoff; // position precedes start of data
        }

        remaining_offset -= buf->length();
        buf = buf->prev();
      }

      gcur_ = buf;
      csetg(gcur_->data(), gcur_->tail() - remaining_offset, gcur_->tail());
      return current_position();
    }

    assert(off > 0);
    size_t remaining_offset = static_cast<size_t>(off);

    if (remaining_offset < static_cast<size_t>(this->egptr() - this->gptr())) {
      assert(reinterpret_cast<uint8_t const*>(this->egptr()) == gcur_->tail());
      csetg(gcur_->data(),
            reinterpret_cast<uint8_t const*>(this->gptr() + remaining_offset),
            gcur_->tail());
      return current_position();
    }

    remaining_offset -= this->egptr() - this->gptr();

    for (buf = buf->next();
         buf != head_;
         buf = buf->next()) {
      if (remaining_offset < buf->length()) {
        gcur_ = buf;
        csetg(gcur_->data(), gcur_->data() + remaining_offset, gcur_->tail());
        return current_position();
      }

      remaining_offset -= buf->length();
    }

    return badoff;
  }

  return badoff;
}

template <typename CharT, typename Traits>
typename IOStreamBuf<CharT,Traits>::pos_type
IOStreamBuf<CharT,Traits>::seekpos(pos_type pos,
                                   std::ios_base::openmode which) {
  return seekoff(off_type(pos), std::ios_base::beg, which);
}

template <typename CharT, typename Traits>
std::streamsize IOStreamBuf<CharT,Traits>::showmanyc() {
  std::streamsize s = this->egptr() - this->gptr();

  for (IOBuf const* buf = gcur_->next(); buf != head_; buf = buf->next()) {
    s += buf->length();
  }

  return s;
}

template <typename CharT, typename Traits>
std::streamsize
IOStreamBuf<CharT,Traits>::xsgetn(char_type* s, std::streamsize count) {
  if (UNLIKELY(count < 0)) {
    return 0;
  }

  std::streamsize copied = 0;

  std::streamsize n = std::min(this->egptr() - this->gptr(),
                               static_cast<off_type>(count));
  Traits::copy(s, this->gptr(), n);
  count -= n;
  copied += n;

  for (IOBuf const* buf = gcur_->next();
       buf != head_ && count > 0;
       buf = buf->next()) {
    n = std::min(static_cast<std::streamsize>(buf->length()),
                 static_cast<off_type>(count));
    Traits::copy(s + copied, reinterpret_cast<CharT const*>(buf->data()), n);
    count -= n;
    copied += n;

    gcur_ = buf;
    csetg(gcur_->data(), gcur_->data() + n, gcur_->tail());
  }

  return copied;
}

template <typename CharT, typename Traits>
void IOStreamBuf<CharT,Traits>::csetg(uint8_t const* gbeg,
                                      uint8_t const* gcurr,
                                      uint8_t const* gend) {
  return this->setg(reinterpret_cast<CharT*>(const_cast<uint8_t*>(gbeg)),
                    reinterpret_cast<CharT*>(const_cast<uint8_t*>(gcurr)),
                    reinterpret_cast<CharT*>(const_cast<uint8_t*>(gend)));
}

} // namespace
