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

#include <memory>
#include <streambuf>

#include <folly/io/IOBuf.h>

namespace folly {

template<typename CharT, typename Traits = std::char_traits<CharT> >
class IOStreamBuf : public std::basic_streambuf<CharT, Traits> {
  // Due to having to merge single-byte subsets of CharT across IOBuf
  // boundaries, prevent the use of IOStreamBuf on multi-byte types for now.
  static_assert(sizeof(CharT) == 1,
          "IOStreamBuf doesn't yet work with multi-byte types");

 public:
  /**
   * Construct IOStreamBuf using the provided IOBuf, which may be chained.
   * The IOStreamBuf does not own the IOBuf nor extend the lifetime of it; you
   * must ensure that the IOBuf provided lasts at least as long as the
   * IOStreamBuf.
   */
  explicit IOStreamBuf(folly::IOBuf const* head);

  IOStreamBuf(IOStreamBuf const&) = default;
  IOStreamBuf& operator=(IOStreamBuf const&) = default;
  void swap(IOStreamBuf<CharT,Traits>&);

  ~IOStreamBuf() override = default;

  using char_type = typename std::basic_streambuf<CharT,Traits>::char_type;
  using int_type = typename std::basic_streambuf<CharT,Traits>::int_type;
  using off_type = typename std::basic_streambuf<CharT,Traits>::off_type;
  using pos_type = typename std::basic_streambuf<CharT,Traits>::pos_type;
  using traits_type = typename std::basic_streambuf<CharT,Traits>::traits_type;

  static IOStreamBuf<CharT,Traits>::pos_type const badoff;

 protected:
  // positioning
  pos_type seekoff(off_type off,
                   std::ios_base::seekdir dir,
                   std::ios_base::openmode which =
                     std::ios_base::in | std::ios_base::out) override;
  pos_type seekpos(pos_type pos,
                   std::ios_base::openmode which =
                     std::ios_base::in | std::ios_base::out) override;

  // get area
  std::streamsize showmanyc() override;
  int_type underflow() override;
  std::streamsize xsgetn(char_type* s, std::streamsize count) override;
  int_type pbackfail(int_type c = traits_type::eof()) override;

  pos_type current_position() const;

  // setg() convenience wrapper that accepts `uint8_t const*` (pointers to
  // IOBuf bytes) and casts them to the streambuf's template type.
  void csetg(uint8_t const* gbeg, uint8_t const* gcurr, uint8_t const* gend);

 private:
  folly::IOBuf const* head_;
  folly::IOBuf const* gcur_; // current get IOBuf
};

}

#include "IOStreamBuf-inl.h"
