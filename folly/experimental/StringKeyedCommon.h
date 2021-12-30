/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// @author: Pavlo Kushnir (pavlo)

#pragma once

#include <memory>

#include <folly/Range.h>

namespace folly {

template <class Alloc>
StringPiece stringPieceDup(StringPiece piece, const Alloc& alloc) {
  auto size = piece.size();
  auto keyDup =
      typename std::allocator_traits<Alloc>::template rebind_alloc<char>(alloc)
          .allocate(size);
  if (size) {
    memcpy(
        keyDup, piece.data(), size * sizeof(typename StringPiece::value_type));
  }
  return StringPiece(keyDup, size);
}

template <class Alloc>
void stringPieceDel(StringPiece piece, const Alloc& alloc) {
  typename std::allocator_traits<Alloc>::template rebind_alloc<char>(alloc)
      .deallocate(const_cast<char*>(piece.data()), piece.size());
}

} // namespace folly
