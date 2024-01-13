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

#pragma once

//  FOLLY_CREATE_EXTERN_ACCESSOR
//
//  Defines a variable template which holds the address of an object or null
//  given a condition which identifies whether that object is expected to have
//  a definition.
//
//  Example:
//
//      extern void foobar(int); // may be defined in another library
//      inline constexpr bool has_foobar = // ...
//
//      namespace myapp {
//      FOLLY_CREATE_EXTERN_ACCESSOR(access_foobar_v, ::foobar);
//      void try_foobar(int num) {
//        if (auto ptr = access_foobar_v<has_foobar>) {
//          ptr(num);
//        }
//      }
//      }
//
//  Remarks:
//
//  This can be done more simply using weak symbols. But weak symbols are non-
//  portable and the rules around the use of weak symbols are complex.
#define FOLLY_CREATE_EXTERN_ACCESSOR(varname, name) \
  struct __folly_extern_accessor_##varname {        \
    template <bool E, typename N>                   \
    static constexpr N* get() noexcept {            \
      if constexpr (E) {                            \
        return &name;                               \
      } else {                                      \
        return nullptr;                             \
      }                                             \
    }                                               \
  };                                                \
  template <bool E, typename N = decltype(name)>    \
  FOLLY_INLINE_VARIABLE constexpr N* varname =      \
      __folly_extern_accessor_##varname::get<E, N>()
